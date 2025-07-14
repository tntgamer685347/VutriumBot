#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <atomic>
#include <mutex>
#include <comdef.h>
#include <Wbemidl.h>
#include <TlHelp32.h>
#include <algorithm>
#pragma comment(lib, "wbemuuid.lib")

SOCKET g_ServerSocket = INVALID_SOCKET;
SOCKET g_ClientSocket = INVALID_SOCKET;
std::atomic<bool> g_ClientConnected = false;
std::atomic<bool> g_SettingsChangedForClient = false;
std::atomic<bool> g_ServerThreadActive = false;
HANDLE g_hServerMainLoopThread = NULL;
const int SERVER_PORT = 13337;
const bool debugBUILD = true;
const bool isPAIDBUILD = false;

std::string g_LicenseExpiration = "N/A";

std::atomic<bool> g_BotEnabledForClient = false;
std::atomic<bool> g_AutoBallCamForClient = false;
std::atomic<bool> g_SpeedflipKickoffForClient = true;
std::atomic<bool> g_PythonMonitoringForClient = false;
std::atomic<bool> g_ClockEnabledForClient = true;
std::string g_SelectedBotNameForClient = "Nexto";
std::mutex g_SettingsMutex;

std::chrono::steady_clock::time_point g_LastGameEventValidityCheck = std::chrono::steady_clock::now();
constexpr float GAME_EVENT_VALIDITY_CHECK_INTERVAL = 3.0f;

std::atomic<uint64_t> g_PacketsSentToClient = 0;
std::atomic<uint64_t> g_BytesSentToClient = 0;
std::atomic<uint64_t> g_ConnectionsEstablished = 0;
std::chrono::steady_clock::time_point g_ServerStartTime;
std::chrono::steady_clock::time_point g_LastPacketSentTime;
std::atomic<bool> g_LastPacketSentTimeValid = false;

UINT g_BotToggleVk = VK_F1;
std::string g_BotToggleKeyName = "F1";
std::atomic<bool> g_IsSettingBotToggleKey = false;

std::string VirtualKeyToString(UINT vkCode);

std::atomic<bool> g_ShutdownSignal = false;
std::atomic<bool> g_SdkAndHooksInitialized = false;

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "includes.h"
#include "RLSDK.h"
#include "Logger.h"
#include <algorithm>

#define UCONST_Pi 3.1415926f
#define URotation180 32768
#define URotationToRadians UCONST_Pi / URotation180

float VectorSize(const SDK::FVectorData& v) {
	return sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
}

SDK::FVectorData VectorNormalize(const SDK::FVectorData& v) {
	float size = VectorSize(v);
	if (size < 0.0001f) {
		return { 1.0f, 1.0f, 1.0f };
	}
	return { v.X / size, v.Y / size, v.Z / size };
}

SDK::FVectorData RotationToVector(const SDK::FRotatorData& R) {
	SDK::FVectorData Vec;
	float fYaw = R.Yaw * URotationToRadians;
	float fPitch = R.Pitch * URotationToRadians;
	float CosPitch = cos(fPitch);
	Vec.X = cos(fYaw) * CosPitch;
	Vec.Y = sin(fYaw) * CosPitch;
	Vec.Z = sin(fPitch);
	return Vec;
}

void GetAxes(const SDK::FRotatorData& R, SDK::FVectorData& X, SDK::FVectorData& Y, SDK::FVectorData& Z) {
	float SY = sin(R.Yaw * URotationToRadians);
	float CY = cos(R.Yaw * URotationToRadians);
	float SP = sin(R.Pitch * URotationToRadians);
	float CP = cos(R.Pitch * URotationToRadians);
	float SR = sin(R.Roll * URotationToRadians);
	float CR = cos(R.Roll * URotationToRadians);

	X.X = CP * CY;
	X.Y = CP * SY;
	X.Z = SP;

	Y.X = SR * SP * CY - CR * SY;
	Y.Y = SR * SP * SY + CR * CY;
	Y.Z = -SR * CP;

	Z.X = -(CR * SP * CY + SR * SY);
	Z.Y = -(CR * SP * SY - SR * CY);
	Z.Z = CR * CP;

	X = VectorNormalize(X);
	Y = VectorNormalize(Y);
	Z = VectorNormalize(Z);
}

SDK::FVectorData VectorSubtract(const SDK::FVectorData& A, const SDK::FVectorData& B) {
	return { A.X - B.X, A.Y - B.Y, A.Z - B.Z };
}

float VectorDotProduct(const SDK::FVectorData& A, const SDK::FVectorData& B) {
	return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

SDK::FVectorData VectorCrossProduct(const SDK::FVectorData& A, const SDK::FVectorData& B) {
	return { A.Y * B.Z - A.Z * B.Y,
			A.Z * B.X - A.X * B.Z,
			A.X * B.Y - A.Y * B.X };
}

std::optional<std::tuple<ImVec2, float>> WorldToScreen(const SDK::FVectorData& WorldLocation, const SDK::FVectorData& CameraLocation,
	const SDK::FRotatorData& CameraRotation, float FOVAngle, float ScreenWidth, float ScreenHeight) {
	SDK::FVectorData AxisX, AxisY, AxisZ;
	GetAxes(CameraRotation, AxisX, AxisY, AxisZ);

	SDK::FVectorData Delta = VectorSubtract(WorldLocation, CameraLocation);

	SDK::FVectorData Transformed;
	Transformed.X = VectorDotProduct(Delta, AxisY);
	Transformed.Y = VectorDotProduct(Delta, AxisZ);
	Transformed.Z = VectorDotProduct(Delta, AxisX);

	if (Transformed.Z <= 0.1f) {
		return std::nullopt;
	}

	float TanHalfFOV = tan(FOVAngle * UCONST_Pi / 360.0f);
	float ScreenCenterX = ScreenWidth / 2.0f;
	float ScreenCenterY = ScreenHeight / 2.0f;

	float X = ScreenCenterX + Transformed.X * (ScreenCenterX / TanHalfFOV) / Transformed.Z;
	float Y = ScreenCenterY + -Transformed.Y * (ScreenCenterX / TanHalfFOV) / Transformed.Z;

	return std::make_tuple(ImVec2(X, Y), Transformed.Z);
}

bool ClipLineSegment(ImVec2& p0, ImVec2& p1, const ImVec2& clipMin, const ImVec2& clipMax) {
	float t0 = 0.0f, t1 = 1.0f;
	float dx = p1.x - p0.x;
	float dy = p1.y - p0.y;

	auto clip_test = [&](float p, float q, float& t_enter, float& t_exit) {
		if (p == 0.0f) {
			return q >= 0.0f;
		}
		float r = q / p;
		if (p < 0.0f) {
			if (r > t_exit) return false;
			if (r > t_enter) t_enter = r;
		}
		else {
			if (r < t_enter) return false;
			if (r < t_exit) t_exit = r;
		}
		return true;
		};

	if (!clip_test(-dx, p0.x - clipMin.x, t0, t1)) return false; 
	if (!clip_test(dx, clipMax.x - p0.x, t0, t1)) return false; 
	if (!clip_test(-dy, p0.y - clipMin.y, t0, t1)) return false; 
	if (!clip_test(dy, clipMax.y - p0.y, t0, t1)) return false; 

	ImVec2 p0_orig = p0;
	p1.x = p0_orig.x + t1 * dx;
	p1.y = p0_orig.y + t1 * dy;
	p0.x = p0_orig.x + t0 * dx;
	p0.y = p0_orig.y + t0 * dy;

	return true;
}

void DrawSphereWorld(const SDK::FVectorData& Center, float Radius, int Segments, const ImU32 Color,
	const SDK::FVectorData& CamLoc, const SDK::FRotatorData& CamRot, float FOV,
	float ScreenWidth, float ScreenHeight)
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	std::vector<std::vector<ImVec2>> points(Segments + 1);

	for (int i = 0; i <= Segments; ++i) {
		float lat = UCONST_Pi * (-0.5f + (float)i / Segments);
		float cosLat = cos(lat);

		points[i].resize(Segments + 1);
		for (int j = 0; j <= Segments; ++j) {
			float lon = 2 * UCONST_Pi * (float)j / Segments;

			SDK::FVectorData point3D;
			point3D.X = Center.X + Radius * cosLat * cos(lon);
			point3D.Y = Center.Y + Radius * cosLat * sin(lon);
			point3D.Z = Center.Z + Radius * sin(lat);

			auto screenPosOpt = WorldToScreen(point3D, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);

			if (screenPosOpt) {
				ImVec2 screenPos = std::get<0>(*screenPosOpt);
				if (screenPos.x >= -100 && screenPos.x <= ScreenWidth + 100 &&
					screenPos.y >= -100 && screenPos.y <= ScreenHeight + 100) {
					points[i][j] = screenPos;
				}
				else {
					points[i][j] = ImVec2(NAN, NAN);
				}
			}
			else {
				points[i][j] = ImVec2(NAN, NAN);
			}
		}
	}

	for (int i = 0; i < Segments; ++i) {
		for (int j = 0; j < Segments; ++j) {
			ImVec2 p1 = points[i][j];
			ImVec2 p2 = points[i + 1][j];
			ImVec2 p3 = points[i][j + 1];

			if (!isnan(p1.x) && !isnan(p3.x)) {
				drawList->AddLine(p1, p3, Color);
			}
			if (!isnan(p1.x) && !isnan(p2.x)) {
				drawList->AddLine(p1, p2, Color);
			}
		}
	}
}

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void DrawWireframeBox(const SDK::FVectorData& Center, const SDK::FRotatorData& Rotation,
	const SDK::FVectorData& HalfExtents, const ImU32 Color,
	const SDK::FVectorData& CamLoc, const SDK::FRotatorData& CamRot, float FOV,
	float ScreenWidth, float ScreenHeight)
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();

	SDK::FVectorData AxisX, AxisY, AxisZ;
	GetAxes(Rotation, AxisX, AxisY, AxisZ);

	SDK::FVectorData corners[8];
	corners[0] = { -HalfExtents.X, -HalfExtents.Y, -HalfExtents.Z };
	corners[1] = { HalfExtents.X, -HalfExtents.Y, -HalfExtents.Z };
	corners[2] = { HalfExtents.X,  HalfExtents.Y, -HalfExtents.Z };
	corners[3] = { -HalfExtents.X,  HalfExtents.Y, -HalfExtents.Z };
	corners[4] = { -HalfExtents.X, -HalfExtents.Y,  HalfExtents.Z };
	corners[5] = { HalfExtents.X, -HalfExtents.Y,  HalfExtents.Z };
	corners[6] = { HalfExtents.X,  HalfExtents.Y,  HalfExtents.Z };
	corners[7] = { -HalfExtents.X,  HalfExtents.Y,  HalfExtents.Z };

	ImVec2 screenCorners[8];
	for (int i = 0; i < 8; ++i) {
		SDK::FVectorData rotatedOffset;
		rotatedOffset.X = AxisX.X * corners[i].X + AxisY.X * corners[i].Y + AxisZ.X * corners[i].Z;
		rotatedOffset.Y = AxisX.Y * corners[i].X + AxisY.Y * corners[i].Y + AxisZ.Y * corners[i].Z;
		rotatedOffset.Z = AxisX.Z * corners[i].X + AxisY.Z * corners[i].Y + AxisZ.Z * corners[i].Z;

		SDK::FVectorData worldCorner = { Center.X + rotatedOffset.X, Center.Y + rotatedOffset.Y, Center.Z + rotatedOffset.Z };

		auto screenPosOpt = WorldToScreen(worldCorner, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);
		if (screenPosOpt) {
			screenCorners[i] = std::get<0>(*screenPosOpt);
		}
		else {
			screenCorners[i] = ImVec2(NAN, NAN);
		}
	}

	int edges[12][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	for (int i = 0; i < 12; ++i) {
		ImVec2 p1 = screenCorners[edges[i][0]];
		ImVec2 p2 = screenCorners[edges[i][1]];

		if (!isnan(p1.x) && !isnan(p1.y) && !isnan(p2.x) && !isnan(p2.y)) {
			drawList->AddLine(p1, p2, Color, 1.5f);
		}
	}
}

void DrawVelocityArrow3D(const SDK::FVectorData& StartPos, const SDK::FVectorData& Velocity,
	float ArrowLengthScale,
	float ArrowHeadLength,
	float ArrowHeadWidth,
	const ImU32 Color,
	const SDK::FVectorData& CamLoc, const SDK::FRotatorData& CamRot, float FOV,
	float ScreenWidth, float ScreenHeight)
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();

	float speed = VectorSize(Velocity);
	if (speed < 10.0f) {
		return;
	}

	SDK::FVectorData velDirection = VectorNormalize(Velocity);
	SDK::FVectorData endPos = {
		StartPos.X + velDirection.X * ArrowLengthScale,
		StartPos.Y + velDirection.Y * ArrowLengthScale,
		StartPos.Z + velDirection.Z * ArrowLengthScale
	};

	auto screenStartOpt = WorldToScreen(StartPos, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);
	auto screenEndOpt = WorldToScreen(endPos, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);

	ImVec2 screenStart = screenStartOpt ? std::get<0>(*screenStartOpt) : ImVec2(NAN, NAN);
	ImVec2 screenEnd = screenEndOpt ? std::get<0>(*screenEndOpt) : ImVec2(NAN, NAN);

	bool shaftVisible = !isnan(screenStart.x) && !isnan(screenStart.y) && !isnan(screenEnd.x) && !isnan(screenEnd.y);

	SDK::FVectorData arrowBasePos = {
		endPos.X - velDirection.X * ArrowHeadLength,
		endPos.Y - velDirection.Y * ArrowHeadLength,
		endPos.Z - velDirection.Z * ArrowHeadLength
	};

	SDK::FVectorData worldUp = { 0.0f, 0.0f, 1.0f };
	SDK::FVectorData perp1 = VectorCrossProduct(velDirection, worldUp);
	float perp1Size = VectorSize(perp1);
	if (perp1Size < 0.1f) { 
		perp1 = VectorCrossProduct(velDirection, { 1.0f, 0.0f, 0.0f });
		perp1 = VectorNormalize(perp1);
	}
	else {
		perp1.X /= perp1Size;
		perp1.Y /= perp1Size;
		perp1.Z /= perp1Size;
	}

	SDK::FVectorData headPoint1_3D = {
		arrowBasePos.X + perp1.X * ArrowHeadWidth / 2.0f,
		arrowBasePos.Y + perp1.Y * ArrowHeadWidth / 2.0f,
		arrowBasePos.Z + perp1.Z * ArrowHeadWidth / 2.0f
	};
	SDK::FVectorData headPoint2_3D = {
		arrowBasePos.X - perp1.X * ArrowHeadWidth / 2.0f,
		arrowBasePos.Y - perp1.Y * ArrowHeadWidth / 2.0f,
		arrowBasePos.Z - perp1.Z * ArrowHeadWidth / 2.0f
	};

	auto screenHead1Opt = WorldToScreen(headPoint1_3D, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);
	auto screenHead2Opt = WorldToScreen(headPoint2_3D, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);

	ImVec2 screenHead1 = screenHead1Opt ? std::get<0>(*screenHead1Opt) : ImVec2(NAN, NAN);
	ImVec2 screenHead2 = screenHead2Opt ? std::get<0>(*screenHead2Opt) : ImVec2(NAN, NAN);

	if (shaftVisible) {
		drawList->AddLine(screenStart, screenEnd, Color, 2.0f);
	}
	bool head1Visible = !isnan(screenHead1.x) && !isnan(screenHead1.y);
	bool head2Visible = !isnan(screenHead2.x) && !isnan(screenHead2.y);

	if (shaftVisible && head1Visible) {
		drawList->AddLine(screenEnd, screenHead1, Color, 2.0f);
	}
	if (shaftVisible && head2Visible) {
		drawList->AddLine(screenEnd, screenHead2, Color, 2.0f);
	}
}

void DrawBoostCircle(const SDK::FVectorData& CarLocation, float BoostAmount,
	const SDK::FVectorData& CamLoc, const SDK::FRotatorData& CamRot, float FOV,
	float ScreenWidth, float ScreenHeight, const ImU32 CircleColor,
	const MemoryManager& pm, const SDK::APRI& CarPRI = SDK::APRI(0), bool useTeamColors = false)
{
	ImDrawList* drawList = ImGui::GetForegroundDrawList();

	auto carScreenPosOpt = WorldToScreen(CarLocation, CamLoc, CamRot, FOV, ScreenWidth, ScreenHeight);
	if (!carScreenPosOpt) return;

	ImVec2 carScreenPos = std::get<0>(*carScreenPosOpt);
	float carDepth = std::get<1>(*carScreenPosOpt);

	if (carDepth <= 0.1f) return;
	if (carScreenPos.x < -50 || carScreenPos.x > ScreenWidth + 50 ||
		carScreenPos.y < -50 || carScreenPos.y > ScreenHeight + 50) {
		return;
	}

	const float fovRadians = FOV * UCONST_Pi / 180.0f;
	const float tanHalfFov = tan(fovRadians / 2.0f);
	const float projectionScale = (ScreenWidth / 2.0f) / tanHalfFov;

	const float baseWorldRadius = 60.0f;
	float radius = (baseWorldRadius * projectionScale) / carDepth;

	const float minRadius = 10.0f;
	const float maxRadius = 40.0f;
	radius = (std::max)(minRadius, (std::min)(radius, maxRadius));

	ImU32 finalCircleColor = CircleColor;

	if (useTeamColors && CarPRI.IsValid()) {
		SDK::ATeamInfo teamInfo = CarPRI.GetTeamInfo(pm);

		static bool logged_team_debug = false;
		if (!logged_team_debug) {
			logged_team_debug = true;
			Logger::Info("Team color debug: PRI valid: " + std::to_string(CarPRI.IsValid()) +
				", TeamInfo valid: " + std::to_string(teamInfo.IsValid()));

			if (teamInfo.IsValid()) {
				SDK::FColorData teamColor = teamInfo.GetColor(pm);
				int32_t teamIndex = teamInfo.GetIndex(pm);

				Logger::Info("Team color values: Index=" + std::to_string(teamIndex) +
					", R=" + std::to_string(teamColor.R) +
					", G=" + std::to_string(teamColor.G) +
					", B=" + std::to_string(teamColor.B));
			}
		}

		if (teamInfo.IsValid()) {
			int32_t teamIndex = teamInfo.GetIndex(pm);

			ImU32 blueTeamColor = IM_COL32(0, 100, 255, 230);  // Blue team
			ImU32 orangeTeamColor = IM_COL32(255, 128, 0, 230); // Orange team

			if (teamIndex == 0) { // Blue team
				finalCircleColor = blueTeamColor;
			}
			else if (teamIndex == 1) { // Orange team
				finalCircleColor = orangeTeamColor;
			}
			else {
				SDK::FColorData teamColor = teamInfo.GetColor(pm);
				if (teamColor.R > 0 || teamColor.G > 0 || teamColor.B > 0) {
					finalCircleColor = IM_COL32(teamColor.R, teamColor.G, teamColor.B, 230);
				}
			}
		}
	}

	drawList->AddCircle(carScreenPos, radius, finalCircleColor, 16, 2.0f);

	if (BoostAmount > 0.0f) {
		float fillPercent = BoostAmount / 100.0f;

		ImVec4 colorFloat = ImGui::ColorConvertU32ToFloat4(finalCircleColor);
		ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			colorFloat.x * 0.3f,
			colorFloat.y * 0.3f,
			colorFloat.z * 0.3f,
			colorFloat.w * 0.7f
		));

		drawList->AddCircleFilled(carScreenPos, radius - 1.0f, bgColor, 16);

		if (fillPercent > 0.001f) {
			int segments = (int)(16 * fillPercent);
			segments = (std::max)(1, segments);

			float minVisibleAngle = 0.1f;
			float angle = (std::max)(minVisibleAngle, fillPercent * 2.0f * UCONST_Pi);

			std::vector<ImVec2> points;
			points.push_back(carScreenPos);

			const float ANGLE_OFFSET = -UCONST_Pi / 2.0f;

			for (int i = 0; i <= segments; i++) {
				float currAngle = (i / (float)segments) * angle + ANGLE_OFFSET;
				float x = carScreenPos.x + cosf(currAngle) * (radius - 1.0f);
				float y = carScreenPos.y + sinf(currAngle) * (radius - 1.0f);
				points.push_back(ImVec2(x, y));
			}

			drawList->AddConvexPolyFilled(points.data(), points.size(), finalCircleColor);
		}

		if (radius >= 18.0f) {
			std::string boostText = std::to_string((int)BoostAmount);
			ImVec2 textSize = ImGui::CalcTextSize(boostText.c_str());

			float fontScale = 1.0f;
			if (carDepth > 2000.0f) {
				fontScale = (std::max)(0.7f, 2000.0f / carDepth);
			}

			textSize.x *= fontScale;
			textSize.y *= fontScale;

			float textPosX = carScreenPos.x - textSize.x / 2;
			float textPosY = carScreenPos.y - textSize.y / 2;

			if (!isnan(textPosX) && !isnan(textPosY) &&
				textPosX > -1000 && textPosX < ScreenWidth + 1000 &&
				textPosY > -1000 && textPosY < ScreenHeight + 1000) {

				drawList->AddText(
					ImVec2(textPosX, textPosY),
					IM_COL32(255, 255, 255, 230),
					boostText.c_str()
				);
			}
		}
	}
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Present oPresent = nullptr;
HWND window = NULL;
WNDPROC oWndProc = nullptr;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView = nullptr;
bool init_hook = false;
bool imgui_init = false;
HMODULE g_hModule = NULL;
bool show = false;

bool g_DrawBallCircle = true;
bool g_Draw3DSphere = false;
bool g_AlwaysDrawBallCircle = false;
bool g_DrawBallPrediction = false;
bool g_AlwaysDrawBallPrediction = false;
bool g_DrawCarHitboxes = false;
bool g_DrawVelocityPointers = false;
bool g_DrawOpponentBoost = false;
bool g_UseTeamColorsForBoost = false;
bool g_DrawTracers = false;
bool g_UseTeamColorsForTracers = false;
bool g_DrawPlayerDistanceText = false;
bool g_DrawBoostPadTimers = false;
bool g_ShowDebugDrawingInfo = false;

ImVec4 g_ColorBallCircle2D = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
ImVec4 g_ColorBallSphere3D = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
ImVec4 g_ColorBallPrediction = ImVec4(1.0f, 0.0f, 1.0f, 0.8f);
ImVec4 g_ColorCarHitbox = ImVec4(0.0f, 1.0f, 1.0f, 0.8f);
ImVec4 g_ColorVelocityArrowBall = ImVec4(1.0f, 0.65f, 0.0f, 0.9f);
ImVec4 g_ColorVelocityArrowCar = ImVec4(0.0f, 1.0f, 0.0f, 0.8f);
ImVec4 g_ColorBoostCircle = ImVec4(0.0f, 0.7f, 1.0f, 0.9f);
ImVec4 g_ColorTracers = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 g_ColorPlayerDistanceText = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImU32 blueTeamColor = IM_COL32(0, 100, 255, 230);
ImU32 orangeTeamColor = IM_COL32(255, 128, 0, 230);

std::unique_ptr<RLSDK> g_pRLSDK = nullptr;

SDK::FieldState g_FieldState;

void SetupImGuiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();

	style.Alpha = 1.0f;
	style.WindowPadding = ImVec2(12.0f, 12.0f);
	style.WindowRounding = 10.0f;
	style.WindowBorderSize = 1.0f;
	style.WindowMinSize = ImVec2(30.0f, 30.0f);
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_Right;

	style.ChildRounding = 8.0f;
	style.ChildBorderSize = 1.0f;

	style.PopupRounding = 8.0f;
	style.PopupBorderSize = 1.0f;

	style.FramePadding = ImVec2(10.0f, 6.0f);
	style.FrameRounding = 6.0f;
	style.FrameBorderSize = 1.0f;

	style.ItemSpacing = ImVec2(10.0f, 8.0f);
	style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
	style.IndentSpacing = 22.0f;
	style.ColumnsMinSpacing = 8.0f;

	style.ScrollbarSize = 16.0f;
	style.ScrollbarRounding = 12.0f;

	style.GrabMinSize = 10.0f;
	style.GrabRounding = 6.0f;

	style.TabRounding = 8.0f;
	style.TabBorderSize = 1.0f;

	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
	style.ColorButtonPosition = ImGuiDir_Right;

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.98f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.65f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.10f, 0.96f);
	colors[ImGuiCol_Border] = ImVec4(0.40f, 0.40f, 0.53f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.13f, 0.18f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.17f, 0.23f, 0.78f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.17f, 0.23f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.14f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.05f, 0.40f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.07f, 0.07f, 0.14f, 0.75f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.08f, 0.80f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.25f, 0.33f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.43f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.45f, 0.53f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.20f, 0.98f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.18f, 0.98f, 0.80f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.26f, 1.00f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.45f, 0.15f, 0.70f, 0.80f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.25f, 0.80f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.35f, 0.90f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.45f, 0.15f, 0.70f, 0.55f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.25f, 0.80f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.65f, 0.35f, 0.90f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.40f, 0.53f, 0.50f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.70f, 0.18f, 0.98f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.85f, 0.26f, 1.00f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.70f, 0.18f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.85f, 0.26f, 1.00f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.90f, 0.32f, 1.00f, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(0.45f, 0.15f, 0.70f, 0.80f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.55f, 0.25f, 0.80f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.65f, 0.30f, 0.85f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.30f, 0.10f, 0.50f, 0.97f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.40f, 0.15f, 0.65f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.66f, 0.66f, 0.66f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.70f, 0.18f, 0.98f, 0.78f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.70f, 0.18f, 0.98f, 0.78f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.85f, 0.26f, 1.00f, 0.78f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.70f, 0.18f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(0.85f, 0.26f, 1.00f, 0.95f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.85f, 0.26f, 1.00f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.75f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);
}

std::string getHWID();
std::string wToString(const WCHAR* wideString);
std::string base64_decode(const std::string& encoded);
bool InitializeSDKAndHooks();

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
	userp->append((char*)contents, size * nmemb);
	return size * nmemb;
}

std::string getHWID() {
	try {
		HRESULT hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
		if (FAILED(hr)) {
			Logger::Error("getHWID: COM initialization failed. HRESULT: " + Logger::to_hex(hr));
			return "COM initialization failed";
		}

		hr = CoInitializeSecurity(
			NULL,
			-1,
			NULL,
			NULL,
			RPC_C_AUTHN_LEVEL_DEFAULT,
			RPC_C_IMP_LEVEL_IMPERSONATE,
			NULL,
			EOAC_NONE,
			NULL
		);
		if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
			Logger::Warning("getHWID: CoInitializeSecurity failed. HRESULT: " + Logger::to_hex(hr) + ". This might be okay if already initialized.");
		}


		IWbemLocator* pLocator = NULL;
		hr = CoCreateInstance(
			CLSID_WbemLocator,
			0,
			CLSCTX_INPROC_SERVER,
			IID_IWbemLocator,
			(LPVOID*)&pLocator
		);

		if (FAILED(hr)) {
			CoUninitialize();
			Logger::Error("getHWID: Failed to create WMI locator. HRESULT: " + Logger::to_hex(hr));
			return "Failed to create WMI locator";
		}

		IWbemServices* pServices = NULL;
		hr = pLocator->ConnectServer(
			_bstr_t(L"ROOT\\CIMV2"),
			NULL,
			NULL,
			0,
			NULL,
			0,
			0,
			&pServices
		);

		if (FAILED(hr)) {
			if (pLocator) pLocator->Release();
			CoUninitialize();
			Logger::Error("getHWID: Failed to connect to WMI. HRESULT: " + Logger::to_hex(hr));
			return "Failed to connect to WMI";
		}

		IEnumWbemClassObject* pEnumerator = NULL;
		hr = pServices->ExecQuery(
			bstr_t(L"WQL"),
			bstr_t(L"SELECT SerialNumber FROM Win32_DiskDrive"),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
			NULL,
			&pEnumerator
		);

		if (FAILED(hr)) {
			if (pServices) pServices->Release();
			if (pLocator) pLocator->Release();
			CoUninitialize();
			Logger::Error("getHWID: Query execution failed. HRESULT: " + Logger::to_hex(hr));
			return "Query execution failed";
		}

		IWbemClassObject* pObject = NULL;
		ULONG uReturn = 0;
		std::string serialNumber;

		if (pEnumerator) {
			while (SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pObject, &uReturn)) && uReturn > 0) {
				VARIANT vtProp;
				VariantInit(&vtProp);
				hr = pObject->Get(L"SerialNumber", 0, &vtProp, 0, 0);

				if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal != NULL) {
					_bstr_t bstrSerial(vtProp.bstrVal, true);
					std::string tempSerial = std::string(bstrSerial);

					tempSerial.erase(0, tempSerial.find_first_not_of(" \n\r\t"));
					tempSerial.erase(tempSerial.find_last_not_of(" \n\r\t") + 1);
					serialNumber = tempSerial;
					VariantClear(&vtProp);
					if (pObject) pObject->Release();
					pObject = NULL;
					break;
				}
				if (pObject) pObject->Release();
				pObject = NULL;
				VariantClear(&vtProp);
			}
		}


		if (pEnumerator) pEnumerator->Release();
		if (pServices) pServices->Release();
		if (pLocator) pLocator->Release();
		CoUninitialize();

		return serialNumber.empty() ? "No serial number found" : serialNumber;
	}
	catch (const std::exception& e) {
		Logger::Error("getHWID: Exception retrieving disk serial number: " + std::string(e.what()));
		CoUninitialize(); 
		return "Error retrieving disk serial number: " + std::string(e.what());
	}
	catch (...) {
		Logger::Error("getHWID: Unknown exception retrieving disk serial number.");
		CoUninitialize();
		return "Unknown error retrieving disk serial number";
	}
}

std::string wToString(const WCHAR* wideString) {
	if (wideString == nullptr) return "";
	int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wideString, -1, nullptr, 0, nullptr, nullptr);
	if (bufferSize == 0) return "";
	std::string narrowString;
	narrowString.resize(bufferSize - 1);
	WideCharToMultiByte(CP_UTF8, 0, wideString, -1, &narrowString[0], bufferSize, nullptr, nullptr);
	return narrowString;
}

std::string base64_decode(const std::string& encoded_string) {
	const std::string base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	size_t in_len = encoded_string.size();
	int i = 0;
	int j = 0;
	size_t in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	std::string ret;

	while (in_ < in_len && (encoded_string[in_] != '=')) {
		if (!(isalnum(encoded_string[in_]) || (encoded_string[in_] == '+') || (encoded_string[in_] == '/'))) {
			in_++;
			continue;
		}
		char_array_4[i++] = encoded_string[in_];
		in_++;
		if (i == 4) {
			for (i = 0; i < 4; i++)
				char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++)
				ret += char_array_3[i];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 4; j++)
			char_array_4[j] = 0;

		for (j = 0; j < 4; j++)
			char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
	}

	return ret;
}

void InitImGui()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	SetupImGuiStyle();
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
}

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	switch (uMsg) {
	case WM_SIZE:
		if (pDevice != NULL && wParam != SIZE_MINIMIZED) {
			if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = NULL; }
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

unsigned __stdcall EjectThread(void* pArg) {
	HMODULE hModule = static_cast<HMODULE>(pArg);
	if (hModule) {
		Logger::Info("EjectThread: Initiating shutdown procedures...");

		if (g_ServerThreadActive) {
			Logger::Info("EjectThread: Stopping server thread...");
			g_ServerThreadActive = false;
			if (g_hServerMainLoopThread != NULL) {
				WaitForSingleObject(g_hServerMainLoopThread, 2000);
				CloseHandle(g_hServerMainLoopThread);
				g_hServerMainLoopThread = NULL;
			}
			Logger::Info("EjectThread: Server thread stopped.");
		}

		if (g_pRLSDK) {
			g_pRLSDK.reset();
			Logger::Info("EjectThread: RLSDK instance reset.");
		}

		if (g_ClientSocket != INVALID_SOCKET) {
			closesocket(g_ClientSocket);
			g_ClientSocket = INVALID_SOCKET;
			Logger::Info("EjectThread: Client socket closed.");
		}
		if (g_ServerSocket != INVALID_SOCKET) {
			closesocket(g_ServerSocket);
			g_ServerSocket = INVALID_SOCKET;
			Logger::Info("EjectThread: Server socket closed.");
		}

		WSACleanup();
		Logger::Info("EjectThread: Winsock cleaned up.");

		Logger::Info("EjectThread: Shutting down Kiero...");
		kiero::shutdown();
		Logger::Info("EjectThread: Kiero shutdown complete.");

		Sleep(100);
		Logger::Info("EjectThread: Calling FreeLibraryAndExitThread.");
		g_ShutdownSignal = true;
		FreeLibraryAndExitThread(hModule, 0);
	}
	return 0;
}

std::string ConstructSettingsJSON() {
	std::string json_str = "{";
	std::string local_selected_bot_name;

	{
		std::lock_guard<std::mutex> lock(g_SettingsMutex);
		local_selected_bot_name = g_SelectedBotNameForClient;
	}

	json_str += "\"bot_enabled\":" + std::string(g_BotEnabledForClient.load() ? "true" : "false") + ",";
	json_str += "\"auto_ball_cam\":" + std::string(g_AutoBallCamForClient.load() ? "true" : "false") + ",";
	json_str += "\"speedflip_kickoff_enabled\":" + std::string(g_SpeedflipKickoffForClient.load() ? "true" : "false") + ",";
	json_str += "\"debug\":" + std::string(g_ShowDebugDrawingInfo ? "true" : "false") + ",";
	json_str += "\"monitoring\":" + std::string(g_PythonMonitoringForClient.load() ? "true" : "false") + ",";
	json_str += "\"selected_bot\":\"" + local_selected_bot_name + "\",";
	json_str += "\"nexto_beta\":1.0,";
	json_str += "\"clock\":" + std::string(g_ClockEnabledForClient.load() ? "true" : "false");

	json_str += "}\n";
	return json_str;
}

unsigned __stdcall ServerMainLoopThread(void* pArg) {
	Logger::Info("ServerThread: Initializing Winsock...");
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		Logger::Error("ServerThread: WSAStartup failed: " + std::to_string(iResult));
		return 1;
	}
	Logger::Info("ServerThread: Winsock Initialized.");

	g_ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (g_ServerSocket == INVALID_SOCKET) {
		Logger::Error("ServerThread: Socket creation failed: " + std::to_string(WSAGetLastError()));
		WSACleanup();
		return 1;
	}
	Logger::Info("ServerThread: Server socket created.");

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	// serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Listen on localhost - DEPRECATED
	if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
		Logger::Error("ServerThread: inet_pton failed for 127.0.0.1: " + std::to_string(WSAGetLastError()));
		closesocket(g_ServerSocket);
		g_ServerSocket = INVALID_SOCKET;
		WSACleanup();
		return 1;
	}
	serverAddr.sin_port = htons(SERVER_PORT);

	iResult = bind(g_ServerSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
	if (iResult == SOCKET_ERROR) {
		Logger::Error("ServerThread: Bind failed: " + std::to_string(WSAGetLastError()));
		closesocket(g_ServerSocket);
		g_ServerSocket = INVALID_SOCKET;
		WSACleanup();
		return 1;
	}
	Logger::Info("ServerThread: Socket bound to 127.0.0.1:" + std::to_string(SERVER_PORT));

	iResult = listen(g_ServerSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		Logger::Error("ServerThread: Listen failed: " + std::to_string(WSAGetLastError()));
		closesocket(g_ServerSocket);
		g_ServerSocket = INVALID_SOCKET;
		WSACleanup();
		return 1;
	}
	Logger::Info("ServerThread: Listening for connections...");

	g_ServerThreadActive = true;
	g_ServerStartTime = std::chrono::steady_clock::now();

	while (g_ServerThreadActive && debugBUILD == false) {
		g_ClientSocket = accept(g_ServerSocket, NULL, NULL);
		if (g_ClientSocket == INVALID_SOCKET) {
			if (g_ServerThreadActive) {
				Logger::Error("ServerThread: Accept failed: " + std::to_string(WSAGetLastError()));
			}
			Sleep(100);
			continue;
		}

		Logger::Info("ServerThread: Client connected.");
		g_ConnectionsEstablished++; 

		char key_buffer[256] = { 0 };
		int bytes_received = recv(g_ClientSocket, key_buffer, sizeof(key_buffer) - 1, 0);

		if (bytes_received > 0) {
			key_buffer[bytes_received] = '\0'; 
			std::string license_key_from_client(key_buffer);
			Logger::Info("ServerThread: Received potential key from client (length " + std::to_string(license_key_from_client.length()) + "): [" + license_key_from_client + "]");

			std::string error_message;
			std::string expiration_date_str;
			bool key_valid = false;

			key_valid = true;

			if (key_valid) {
				Logger::Info("ServerThread: License key VERIFIED. Expiration: " + expiration_date_str);
				g_LicenseExpiration = expiration_date_str;

				if (!g_SdkAndHooksInitialized.load(std::memory_order_relaxed)) {
					Logger::Info("ServerThread: Authenticated client. Initializing RLSDK and Hooks...");
					if (InitializeSDKAndHooks()) {
						Logger::Info("ServerThread: RLSDK and Hooks initialized successfully.");
					}
					else {
						Logger::Error("ServerThread: RLSDK and Hooks initialization FAILED. Disconnecting client.");
						closesocket(g_ClientSocket);
						g_ClientSocket = INVALID_SOCKET;
						continue;
					}
				}
				else {
					Logger::Info("ServerThread: RLSDK and Hooks already initialized.");
				}

				if (g_SdkAndHooksInitialized.load(std::memory_order_relaxed)) {
					Logger::Info("ServerThread: Client authenticated and SDK ready. Starting settings communication loop.");
					g_ClientConnected = true; 
					{
						std::lock_guard<std::mutex> lock(g_SettingsMutex);
						g_SettingsChangedForClient = true; 
					}
					g_LastPacketSentTime = std::chrono::steady_clock::now();
					g_LastPacketSentTimeValid = false;

					while (g_ClientConnected && g_ServerThreadActive) {
						bool settings_changed_flag = false;
						{
							std::lock_guard<std::mutex> lock(g_SettingsMutex);
							settings_changed_flag = g_SettingsChangedForClient;
						}

						bool should_send_settings = settings_changed_flag;
						auto now_comm_loop = std::chrono::steady_clock::now();

						if (!settings_changed_flag && g_LastPacketSentTimeValid.load(std::memory_order_relaxed)) {
							auto time_since_last_send = std::chrono::duration_cast<std::chrono::seconds>(now_comm_loop - g_LastPacketSentTime);
							if (time_since_last_send.count() >= 10) {
								should_send_settings = true;
							}
						}

						if (should_send_settings) {
							std::string json_payload = ConstructSettingsJSON();
							int sendResult = ::send(g_ClientSocket, json_payload.c_str(), (int)json_payload.length(), 0);
							if (sendResult == SOCKET_ERROR) {
								Logger::Error("ServerThread: Send failed with error: " + std::to_string(WSAGetLastError()));
								g_ClientConnected = false;
							}
							else {
								g_PacketsSentToClient++;
								g_BytesSentToClient += static_cast<uint64_t>(json_payload.length());
								g_LastPacketSentTime = std::chrono::steady_clock::now();
								g_LastPacketSentTimeValid = true;
								{
									std::lock_guard<std::mutex> lock(g_SettingsMutex);
									g_SettingsChangedForClient = false;
								}
							}
						}
						Sleep(8);
					}

				}
				else {
					Logger::Error("ServerThread: SDK not initialized after key verification. Disconnecting client.");
					closesocket(g_ClientSocket);
					g_ClientSocket = INVALID_SOCKET;
					continue;
				}

			}
			else {
				Logger::Warning("ServerThread: License key INVALID or verification failed. Message: [" + error_message + "]. Disconnecting client.");
				closesocket(g_ClientSocket);
				g_ClientSocket = INVALID_SOCKET;
				continue;
			}

		}
		else if (bytes_received == 0) {
			Logger::Info("ServerThread: Client disconnected before sending key (recv returned 0).");
			g_ClientSocket = INVALID_SOCKET; 
			continue; 
		}
		else { 
			Logger::Error("ServerThread: recv failed when expecting key: " + std::to_string(WSAGetLastError()));
			closesocket(g_ClientSocket);
			g_ClientSocket = INVALID_SOCKET;
			continue;
		}


		if (g_ClientSocket != INVALID_SOCKET) {
			closesocket(g_ClientSocket);
			g_ClientSocket = INVALID_SOCKET;
		}
		g_ClientConnected = false;
		if (g_ServerThreadActive) {
			Logger::Info("ServerThread: Client session ended. Waiting for new connection...");
		}
	}

	if (g_ServerSocket != INVALID_SOCKET) {
		closesocket(g_ServerSocket);
		g_ServerSocket = INVALID_SOCKET;
	}
	WSACleanup();
	Logger::Info("ServerThread: Exiting.");
	if (debugBUILD) {
		if (!g_SdkAndHooksInitialized.load(std::memory_order_relaxed)) {
			Logger::Info("ServerThread: Authenticated client. Initializing RLSDK and Hooks...");
			if (InitializeSDKAndHooks()) {
				Logger::Info("ServerThread: RLSDK and Hooks initialized successfully.");
			}
			else {
				Logger::Error("ServerThread: RLSDK and Hooks initialization FAILED. Disconnecting client.");
				closesocket(g_ClientSocket);
				g_ClientSocket = INVALID_SOCKET;
			}
		}
		else {
			Logger::Info("ServerThread: RLSDK and Hooks already initialized.");
		}
	}
	return 0;
}

void ClampBallVelocity(SDK::FVectorData& vel, float maxSpeed)
{
	float speedSq = vel.X * vel.X + vel.Y * vel.Y + vel.Z * vel.Z;
	if (speedSq > maxSpeed * maxSpeed)
	{
		float speed = std::sqrt(speedSq);
		float scale = maxSpeed / speed;
		vel.X *= scale;
		vel.Y *= scale;
		vel.Z *= scale;
	}
}

inline float MaxF(float a, float b) { return (a > b) ? a : b; }
inline float MinF(float a, float b) { return (a < b) ? a : b; }

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	if (!imgui_init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)) && pDevice != nullptr)
		{
			if (!g_SdkAndHooksInitialized.load()) {
				return oPresent(pSwapChain, SyncInterval, Flags); 
			}

			pDevice->GetImmediateContext(&pContext);
			if (pContext == nullptr) {
				Logger::Error("hkPresent Error: Failed to get Immediate Context.");
				if (pDevice) { pDevice->Release(); pDevice = nullptr; }
				return oPresent(pSwapChain, SyncInterval, Flags); 
			}

			DXGI_SWAP_CHAIN_DESC sd;
			if FAILED(pSwapChain->GetDesc(&sd)) {
				Logger::Error("hkPresent Error: Failed to get SwapChain Description.");
				if (pContext) { pContext->Release(); pContext = nullptr; }
				if (pDevice) { pDevice->Release(); pDevice = nullptr; }
				return oPresent(pSwapChain, SyncInterval, Flags); 
			}
			window = sd.OutputWindow;

			ID3D11Texture2D* pBackBuffer = nullptr;
			if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer)) && pBackBuffer != nullptr)
			{
				HRESULT hr = pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
				pBackBuffer->Release(); 
				if FAILED(hr) {
					Logger::Error("hkPresent Error: CreateRenderTargetView failed. HRESULT: " + Logger::to_hex(hr));
					mainRenderTargetView = nullptr;
					if (pContext) { pContext->Release(); pContext = nullptr; } 
					if (pDevice) { pDevice->Release(); pDevice = nullptr; }
					return oPresent(pSwapChain, SyncInterval, Flags); 
				}
			}
			else {
				Logger::Error("hkPresent Error: Failed to get back buffer.");
				if (pContext) { pContext->Release(); pContext = nullptr; }
				if (pDevice) { pDevice->Release(); pDevice = nullptr; }
				return oPresent(pSwapChain, SyncInterval, Flags); 
			}

			oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
			if (oWndProc == NULL) {
				Logger::Error("hkPresent Error: SetWindowLongPtr failed.");
				if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = nullptr; } 
				if (pContext) { pContext->Release(); pContext = nullptr; }
				if (pDevice) { pDevice->Release(); pDevice = nullptr; }
				return oPresent(pSwapChain, SyncInterval, Flags); 
			}

			InitImGui();
			imgui_init = true;
			Logger::Info("ImGui Initialized.");
		}
		else
		{
			Logger::Error("hkPresent Error: Failed to get D3D11 Device.");
			return oPresent(pSwapChain, SyncInterval, Flags);
		}
	}

	if (GetAsyncKeyState(VK_INSERT) & 1) {
		show = !show;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	if (g_IsSettingBotToggleKey.load()) {
		ImGui::SetTooltip("Press a key to set as bot toggle (ESC to cancel). Current: %s", g_BotToggleKeyName.c_str());

		bool keySetThisFrame = false;
		UINT newVk = 0;

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { 
			g_IsSettingBotToggleKey.store(false);
			keySetThisFrame = true; 
		}

		if (!keySetThisFrame) {
			for (int vk_candidate = VK_F1; vk_candidate <= VK_F12; ++vk_candidate) {
				if (GetAsyncKeyState(vk_candidate) & 1) {
					newVk = vk_candidate;
					keySetThisFrame = true;
					break;
				}
			}
		}

		if (!keySetThisFrame) {
			for (int vk_candidate = 'A'; vk_candidate <= 'Z'; ++vk_candidate) {
				if (GetAsyncKeyState(vk_candidate) & 1) {
					newVk = vk_candidate;
					keySetThisFrame = true;
					break;
				}
			}
		}
		if (!keySetThisFrame) {
			for (int vk_candidate = '0'; vk_candidate <= '9'; ++vk_candidate) {
				if (GetAsyncKeyState(vk_candidate) & 1) {
					newVk = vk_candidate;
					keySetThisFrame = true;
					break;
				}
			}
		}

		if (!keySetThisFrame) {
			for (int vk_candidate = VK_NUMPAD0; vk_candidate <= VK_NUMPAD9; ++vk_candidate) {
				if (GetAsyncKeyState(vk_candidate) & 1) {
					newVk = vk_candidate;
					keySetThisFrame = true;
					break;
				}
			}
		}

		if (!keySetThisFrame) {
			const UINT special_keys_to_check[] = {
				VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
				VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN,
				VK_SPACE, VK_RETURN,
				VK_SHIFT, VK_CONTROL, VK_MENU
			};
			for (UINT vk_candidate : special_keys_to_check) {
				if (GetAsyncKeyState(vk_candidate) & 1) {
					newVk = vk_candidate;
					keySetThisFrame = true;
					break;
				}
			}
		}

		if (keySetThisFrame && newVk != 0) {
			g_BotToggleVk = newVk;
			g_BotToggleKeyName = VirtualKeyToString(newVk);
			g_IsSettingBotToggleKey.store(false);
		}

	}
	else {
		if (g_BotToggleVk != 0 && (GetAsyncKeyState(g_BotToggleVk) & 1)) {
			g_BotEnabledForClient.store(!g_BotEnabledForClient.load());
			g_SettingsChangedForClient = true;
			Logger::Info("Bot enabled toggled by key press to: " + std::string(g_BotEnabledForClient.load() ? "true" : "false"));
		}
	}

	if ((g_DrawBallCircle || g_Draw3DSphere || g_DrawBallPrediction || g_AlwaysDrawBallCircle || g_DrawCarHitboxes || g_DrawVelocityPointers || g_DrawOpponentBoost || g_DrawTracers || g_DrawPlayerDistanceText || g_DrawBoostPadTimers)
		&& g_SdkAndHooksInitialized.load() && g_pRLSDK)
	{
		SDK::AGameEvent ge = g_pRLSDK->GetCurrentGameEvent();
		if (ge.IsValid()) {
			try {
				auto currentTime = std::chrono::steady_clock::now();
				std::chrono::duration<float> elapsedSinceLastCheck = currentTime - g_LastGameEventValidityCheck;
				bool shouldRecheckValidity = elapsedSinceLastCheck.count() >= GAME_EVENT_VALIDITY_CHECK_INTERVAL;

				if (shouldRecheckValidity) {
					g_LastGameEventValidityCheck = currentTime;
					g_pRLSDK->UpdateCurrentGameEvent(ge.Address);
					ge = g_pRLSDK->GetCurrentGameEvent();
				}

				auto balls = ge.GetBalls(g_pRLSDK->GetMemoryManager());
				if (balls.empty() || !balls[0].IsValid()) {
					throw std::runtime_error("Ball not found or invalid");
				}
				SDK::ABall ball = balls[0];
				SDK::FVectorData ballLoc = ball.GetLocation(g_pRLSDK->GetMemoryManager());
				SDK::FVectorData ballVel = ball.GetVelocity(g_pRLSDK->GetMemoryManager());
				SDK::FVectorData ballAngVel = ball.GetAngularVelocity(g_pRLSDK->GetMemoryManager());

				auto localPlayers = ge.GetLocalPlayers(g_pRLSDK->GetMemoryManager());
				if (localPlayers.empty() || !localPlayers[0].IsValid()) {
					throw std::runtime_error("Local player controller not found or invalid");
				}
				SDK::APlayerController pc = localPlayers[0];

				SDK::ACamera playerCamera = pc.GetPlayerCamera(g_pRLSDK->GetMemoryManager());
				if (!playerCamera.IsValid()) {
					throw std::runtime_error("PlayerCamera actor not found or invalid");
				}

				SDK::APRI pri = pc.GetPRI(g_pRLSDK->GetMemoryManager());
				SDK::FProfileCameraSettings camSettings;
				
				if (pri.IsValid()) {
					auto settingsOpt = pri.GetCameraSettings(g_pRLSDK->GetMemoryManager());
					if (settingsOpt) {
						camSettings = *settingsOpt;
					} else {
						camSettings.FOV = 90.0f;
						camSettings.Height = 100.0f;
						camSettings.Pitch = -3.0f;
						camSettings.Distance = 270.0f;
						camSettings.Stiffness = 0.5f;
						camSettings.SwivelSpeed = 2.5f;
						camSettings.TransitionSpeed = 1.0f;
					}
				} else {
					camSettings.FOV = 90.0f;
					camSettings.Height = 100.0f;
					camSettings.Pitch = -3.0f;
					camSettings.Distance = 270.0f;
					camSettings.Stiffness = 0.5f;
					camSettings.SwivelSpeed = 2.5f;
					camSettings.TransitionSpeed = 1.0f;
				}

				SDK::FVectorData camLocation = playerCamera.GetLocation(g_pRLSDK->GetMemoryManager());
				SDK::FRotatorData camRotation = playerCamera.GetRotation(g_pRLSDK->GetMemoryManager());

				ImVec2 screenSize = ImGui::GetIO().DisplaySize;
				if (screenSize.x <= 0 || screenSize.y <= 0) {
					throw std::runtime_error("Invalid screen size");
				}

				auto ballScreenPosOpt = WorldToScreen(
					ballLoc, camLocation, camRotation, camSettings.FOV,
					screenSize.x, screenSize.y
				);

				ImVec2 ballScreenPos = ImVec2(NAN, NAN);
				float ballTransformedZ = -1.0f;
				bool ballOnScreen = false;

				if (ballScreenPosOpt) {
					std::tie(ballScreenPos, ballTransformedZ) = *ballScreenPosOpt;
					ballOnScreen = ballScreenPos.x >= 0 && ballScreenPos.x <= screenSize.x &&
						ballScreenPos.y >= 0 && ballScreenPos.y <= screenSize.y;
				}

				const float ballRadiusWorld = 91.25f;
				const float circleRadiusScreen = 20.0f;
				const int sphereSegments = 24;
				const ImU32 color = IM_COL32(255, 255, 0, 255);
				ImDrawList* drawList = ImGui::GetForegroundDrawList();

				if (g_Draw3DSphere) {
					DrawSphereWorld(ballLoc, ballRadiusWorld, sphereSegments, ImGui::ColorConvertFloat4ToU32(g_ColorBallSphere3D),
						camLocation, camRotation, camSettings.FOV, 
						screenSize.x, screenSize.y);
				}
				else if (g_DrawBallCircle && ballScreenPosOpt && ballOnScreen)
				{
					const float fovRadians = camSettings.FOV * UCONST_Pi / 180.0f; 
					const float tanHalfFov = tan(fovRadians / 2.0f);
					const float projectionScale = (screenSize.x / 2.0f) / tanHalfFov;

					float dynamicRadius = 0.0f;
					if (ballTransformedZ > 0.1f) {
						dynamicRadius = (ballRadiusWorld * projectionScale) / ballTransformedZ;
					}

					const float minRadius = 2.0f;
					const float maxRadius = screenSize.y;
					dynamicRadius = (std::max)(minRadius, (std::min)(dynamicRadius, maxRadius));

					drawList->AddCircle(ballScreenPos, dynamicRadius, ImGui::ColorConvertFloat4ToU32(g_ColorBallCircle2D), 16, 2.0f);
				}

				if (g_DrawBallPrediction) {
					const float GRAVITY_Z = -650.0f;
					const float BALL_RADIUS = 91.25f;
					const float COEFFICIENT_OF_RESTITUTION = 0.6f;
					const float GROUND_FRICTION_FACTOR = 0.98f;
					const float SPIN_DAMPING_FACTOR = 0.995f;

					const float MAX_BALL_SPEED = 6000.0f;

					const float FLOOR_Z_PHYSICAL = 0.0f;
					const float CEILING_Z_PHYSICAL = 2044.0f;
					const float WALL_X_LIMIT_PHYSICAL = 4096.0f;
					const float WALL_Y_LIMIT_PHYSICAL = 5120.0f;
					
					const float GOAL_HEIGHT_PHYSICAL = 642.775f;
					const float GOAL_WIDTH_PHYSICAL = 1786.0f;
					const float GOAL_DEPTH_PHYSICAL = 880.0f;
					
					const float FLOOR_Z_EFFECTIVE = FLOOR_Z_PHYSICAL + BALL_RADIUS;
					const float CEILING_Z_EFFECTIVE = CEILING_Z_PHYSICAL - BALL_RADIUS;
					const float WALL_X_EFFECTIVE = WALL_X_LIMIT_PHYSICAL - BALL_RADIUS;
					const float WALL_Y_EFFECTIVE = WALL_Y_LIMIT_PHYSICAL - BALL_RADIUS;

					const float PREDICTION_TIME_SECONDS = 3.0f;
					const float TIME_STEP = 1.0f / 120.0f;
					const int MAX_STEPS = static_cast<int>(PREDICTION_TIME_SECONDS / TIME_STEP);

					SDK::FVectorData currentPos = ballLoc;
					SDK::FVectorData currentVel = ballVel;
					SDK::FVectorData currentAngVel = ballAngVel;

					bool hasLastValidPos = false;
					ImVec2 lastValidScreenPos;
					int lastCollisionType = 0;

					for (int step = 0; step < MAX_STEPS; ++step) {
						SDK::FVectorData prevPos = currentPos;

						float spin_damp_step = powf(SPIN_DAMPING_FACTOR, TIME_STEP);
						currentAngVel.X *= spin_damp_step;
						currentAngVel.Y *= spin_damp_step;
						currentAngVel.Z *= spin_damp_step;
						
						currentVel.Z += GRAVITY_Z * TIME_STEP; 
						ClampBallVelocity(currentVel, MAX_BALL_SPEED);

						currentPos.X += currentVel.X * TIME_STEP;
						currentPos.Y += currentVel.Y * TIME_STEP;
						currentPos.Z += currentVel.Z * TIME_STEP;

						lastCollisionType = 0;
						bool collidedThisStep = false;

						if (currentPos.Z <= FLOOR_Z_EFFECTIVE) {
							currentPos.Z = FLOOR_Z_EFFECTIVE;
							currentVel.Z *= -COEFFICIENT_OF_RESTITUTION;
							currentVel.X *= GROUND_FRICTION_FACTOR;
							currentVel.Y *= GROUND_FRICTION_FACTOR;


							currentAngVel.X *= 0.3f;
							currentAngVel.Y *= 0.3f;
							
							lastCollisionType = 1;
							collidedThisStep = true;
						}
						else if (currentPos.Z >= CEILING_Z_EFFECTIVE) {
							currentPos.Z = CEILING_Z_EFFECTIVE;
							currentVel.Z *= -COEFFICIENT_OF_RESTITUTION;
							lastCollisionType = 2; 
							collidedThisStep = true;
						}

						if (std::abs(currentPos.X) >= WALL_X_EFFECTIVE) {
							currentPos.X = (currentPos.X > 0 ? 1.f : -1.f) * WALL_X_EFFECTIVE;
							currentVel.X *= -COEFFICIENT_OF_RESTITUTION;
							lastCollisionType = 3;
							collidedThisStep = true;
						}
						if (std::abs(currentPos.Y) >= WALL_Y_EFFECTIVE) {
							bool inGoalAreaX = std::abs(currentPos.X) < (GOAL_WIDTH_PHYSICAL / 2.0f - BALL_RADIUS);
							bool inGoalAreaZ = currentPos.Z < (GOAL_HEIGHT_PHYSICAL - BALL_RADIUS);

							if (inGoalAreaX && inGoalAreaZ) {
								float goalBackPlaneY = (currentPos.Y > 0) ?
									(WALL_Y_LIMIT_PHYSICAL + GOAL_DEPTH_PHYSICAL - BALL_RADIUS) :
									-(WALL_Y_LIMIT_PHYSICAL + GOAL_DEPTH_PHYSICAL - BALL_RADIUS);
								
								if ((currentPos.Y > 0 && currentPos.Y >= goalBackPlaneY && currentVel.Y > 0) ||
									(currentPos.Y < 0 && currentPos.Y <= goalBackPlaneY && currentVel.Y < 0)) 
								{
									currentPos.Y = goalBackPlaneY;
									currentVel.Y *= -COEFFICIENT_OF_RESTITUTION;
									lastCollisionType = 3;
									collidedThisStep = true;
								}
							}
							else {
								currentPos.Y = (currentPos.Y > 0 ? 1.f : -1.f) * WALL_Y_EFFECTIVE;
								currentVel.Y *= -COEFFICIENT_OF_RESTITUTION;
								lastCollisionType = 3;
								collidedThisStep = true;
							}
						}


						auto currentScreenPosOpt = WorldToScreen(currentPos, camLocation, camRotation, camSettings.FOV, screenSize.x, screenSize.y); 

						if (currentScreenPosOpt) {
							ImVec2 currentScreenPos = std::get<0>(*currentScreenPosOpt);
							bool currentPosValid = !isnan(currentScreenPos.x) && !isnan(currentScreenPos.y) &&
								currentScreenPos.x >= -100 && currentScreenPos.x <= screenSize.x + 100 &&
								currentScreenPos.y >= -100 && currentScreenPos.y <= screenSize.y + 100;

							if (hasLastValidPos && currentPosValid) {
								float speed = std::sqrt(currentVel.X * currentVel.X + 
									currentVel.Y * currentVel.Y +
									currentVel.Z * currentVel.Z);
								float speedRatio = MinF(1.0f, speed / 2000.0f);

								bool isInGoal = std::abs(currentPos.Y) > WALL_Y_LIMIT_PHYSICAL && 
									std::abs(currentPos.X) < (GOAL_WIDTH_PHYSICAL / 2.f - BALL_RADIUS) &&
									currentPos.Z < (GOAL_HEIGHT_PHYSICAL - BALL_RADIUS);

								float t = static_cast<float>(step) / MAX_STEPS;
								int r, g, b, a;

								if (isInGoal) {
									r = 255; g = 20; b = 20; a = 220;
								}
								else {
									r = static_cast<int>(0 + (0 - 0) * t);                           
									g = static_cast<int>((255 - speedRatio * 100) * (1.0f - t));     
									b = static_cast<int>(128 + (255 - 128) * t + speedRatio * 100);  
									a = static_cast<int>(220 + (50 - 220) * t);                      
								}
								

								float thickness = 2.0f + speedRatio * 2.0f;

								static int lastDrawnCollisionType = 0;
								
								if (collidedThisStep && lastCollisionType != lastDrawnCollisionType) {
									ImU32 collisionColor;
									switch (lastCollisionType) {
										case 1: collisionColor = IM_COL32(255, 165, 0, 220); break; 
										case 2: collisionColor = IM_COL32(255, 100, 100, 220); break; 
										case 3: collisionColor = IM_COL32(100, 255, 100, 220); break; 
										default: collisionColor = IM_COL32(255, 255, 255, 220); 
									}
									drawList->AddCircle(currentScreenPos, 3.0f, collisionColor, 8, 1.5f); 
									lastDrawnCollisionType = lastCollisionType;
								}
								drawList->AddLine(lastValidScreenPos, currentScreenPos, IM_COL32(r, g, b, a), thickness); 
							}

							if (currentPosValid) {
								lastValidScreenPos = currentScreenPos;
								hasLastValidPos = true;
							}
							else {
								hasLastValidPos = false;
							}
						}
						else {
							hasLastValidPos = false;
						}
					}
				}

				if (show && g_ShowDebugDrawingInfo) { 
					ImGui::Separator(); 
					ImGui::Text("Camera Info (Live):");
					ImGui::Text("  Location: X=%.1f, Y=%.1f, Z=%.1f", camLocation.X, camLocation.Y, camLocation.Z);
					ImGui::Text("  Rotation: P=%d, Y=%d, R=%d", camRotation.Pitch, camRotation.Yaw, camRotation.Roll);
					ImGui::Text("  FOV (from PRI Settings): %.1f", camSettings.FOV);

					if (ballScreenPosOpt && ballOnScreen) {
						ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
							"Ball screen pos: (%.1f, %.1f) Depth: %.1f Transformed.Z: %.1f",
							ballScreenPos.x, ballScreenPos.y, ballTransformedZ, ballTransformedZ);
					}
					else if (ballScreenPosOpt) {
						ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
							"Ball off screen: (%.1f, %.1f) Depth: %.1f Transformed.Z: %.1f",
							ballScreenPos.x, ballScreenPos.y, ballTransformedZ, ballTransformedZ);
					}
					else {
						ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
							"Ball projection failed (Behind camera?) Transformed.Z: %.1f", ballTransformedZ);
					}
				}

				if (g_DrawCarHitboxes) {
					auto cars = ge.GetCars(g_pRLSDK->GetMemoryManager());
					const SDK::FVectorData octaneHalfExtents = { 118.0f / 2.0f, 84.2f / 2.0f, 36.16f / 2.0f }; 
					const ImU32 hitboxColor = ImGui::ColorConvertFloat4ToU32(g_ColorCarHitbox); 

					for (const auto& car : cars) {
						if (!car.IsValid()) continue;

						SDK::FVectorData carLocation = car.GetLocation(g_pRLSDK->GetMemoryManager());
						SDK::FRotatorData carRotation = car.GetRotation(g_pRLSDK->GetMemoryManager());
						DrawWireframeBox(carLocation, carRotation, octaneHalfExtents, hitboxColor,
							camLocation, camRotation, camSettings.FOV, screenSize.x, screenSize.y);
					}
				}

				if (g_DrawBoostPadTimers && g_SdkAndHooksInitialized.load() && g_pRLSDK) {
					for (const auto& pad : g_FieldState.BoostPads) {
						if (!pad.IsActive) {
							auto remaining = pad.GetRemainingSeconds();
							if (remaining) {
								SDK::FVectorData padLocation;
								padLocation.X = pad.Location.X;
								padLocation.Y = pad.Location.Y;
								padLocation.Z = pad.Location.Z + 100.0f;
								
								auto screenPosOpt = WorldToScreen(padLocation, camLocation, camRotation, 
									camSettings.FOV, screenSize.x, screenSize.y);
								
								if (screenPosOpt) {
									ImVec2 screenPos = std::get<0>(*screenPosOpt);
									
									if (screenPos.x >= 0 && screenPos.x <= screenSize.x &&
										screenPos.y >= 0 && screenPos.y <= screenSize.y) {
										
										char timerText[16];
										snprintf(timerText, sizeof(timerText), "%.1f", *remaining);
										
										ImU32 textColor = pad.IsBigPad ? 
											IM_COL32(255, 215, 0, 255) : 
											IM_COL32(255, 255, 255, 255);
										
										float fontSize = 18.0f;
										ImFont* font = ImGui::GetFont();
										ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, timerText);
										
										ImGui::GetForegroundDrawList()->AddRectFilled(
											ImVec2(screenPos.x - textSize.x/2 - 5, screenPos.y - textSize.y/2 - 3),
											ImVec2(screenPos.x + textSize.x/2 + 5, screenPos.y + textSize.y/2 + 3),
											IM_COL32(0, 0, 0, 200)
										);
										
										ImU32 outlineColor = pad.IsBigPad ? 
											IM_COL32(255, 215, 0, 150) :
											IM_COL32(200, 200, 200, 150);
										
										ImGui::GetForegroundDrawList()->AddRect(
											ImVec2(screenPos.x - textSize.x/2 - 5, screenPos.y - textSize.y/2 - 3),
											ImVec2(screenPos.x + textSize.x/2 + 5, screenPos.y + textSize.y/2 + 3),
											outlineColor, 0.0f, 0, 1.5f
										);
										
										ImU32 glowColor = pad.IsBigPad ? 
											IM_COL32(255, 215, 0, 80) :
											IM_COL32(200, 200, 200, 80); 
											
										for (int i = 0; i < 4; i++) {
											float offset = 1.0f + i * 0.5f;
											ImGui::GetForegroundDrawList()->AddText(
												font,
												fontSize,
												ImVec2(screenPos.x - textSize.x/2 - offset, screenPos.y - textSize.y/2),
												glowColor,
												timerText
											);
											ImGui::GetForegroundDrawList()->AddText(
												font,
												fontSize,
												ImVec2(screenPos.x - textSize.x/2 + offset, screenPos.y - textSize.y/2),
												glowColor,
												timerText
											);
											ImGui::GetForegroundDrawList()->AddText(
												font,
												fontSize,
												ImVec2(screenPos.x - textSize.x/2, screenPos.y - textSize.y/2 - offset),
												glowColor,
												timerText
											);
											ImGui::GetForegroundDrawList()->AddText(
												font,
												fontSize,
												ImVec2(screenPos.x - textSize.x/2, screenPos.y - textSize.y/2 + offset),
												glowColor,
												timerText
											);
										}
										
										ImGui::GetForegroundDrawList()->AddText(
											font,
											fontSize,
											ImVec2(screenPos.x - textSize.x/2, screenPos.y - textSize.y/2),
											textColor, 
											timerText
										);
									}
								}
							}
						}
					}
				}

				if (g_DrawTracers) {
					auto allCars = ge.GetCars(g_pRLSDK->GetMemoryManager());
					auto localPlayerControllers = ge.GetLocalPlayers(g_pRLSDK->GetMemoryManager());

					const ImU32 defaultTracersColor = ImGui::ColorConvertFloat4ToU32(g_ColorTracers);
					float tracerThickness = 2.7f; 

					SDK::ACar localPlayerActualCar(0);
					uintptr_t localPlayerPRIAddress = 0;

					if (!localPlayerControllers.empty()) {
						SDK::APlayerController pc = localPlayerControllers[0];
						if (pc.IsValid()) {
							SDK::APRI localPRI = pc.GetPRI(g_pRLSDK->GetMemoryManager());
							if (localPRI.IsValid()) {
								localPlayerPRIAddress = localPRI.Address;
							}
						}
					}

					if (localPlayerPRIAddress == 0 && !allCars.empty()) {
						localPlayerActualCar = allCars[0];
					} else if (localPlayerPRIAddress != 0) {
						for (const auto& carInstance : allCars) {
							if (carInstance.IsValid()) {
								SDK::APRI carInstancePRI = carInstance.GetPRI(g_pRLSDK->GetMemoryManager());
								if (carInstancePRI.IsValid() && carInstancePRI.Address == localPlayerPRIAddress) {
									localPlayerActualCar = carInstance;
									break;
								}
							}
						}
					}

					ImVec2 tracerStartPointScreenActual = ImVec2(NAN, NAN);

					if (localPlayerActualCar.IsValid()) {
						SDK::FVectorData localCarWorldLocation = localPlayerActualCar.GetLocation(g_pRLSDK->GetMemoryManager());
						float fovForW2S = camSettings.FOV;

						std::optional<std::tuple<ImVec2, float>> localCarScreenOpt = WorldToScreen(
							localCarWorldLocation, camLocation, camRotation, fovForW2S, screenSize.x, screenSize.y);

						if (localCarScreenOpt.has_value()) {
							tracerStartPointScreenActual = std::get<0>(localCarScreenOpt.value());
							if (tracerStartPointScreenActual.x < 0 || tracerStartPointScreenActual.x > screenSize.x ||
								tracerStartPointScreenActual.y < 0 || tracerStartPointScreenActual.y > screenSize.y) {
								tracerStartPointScreenActual = ImVec2(NAN, NAN);
							}
						}
					}

					if (!isnan(tracerStartPointScreenActual.x) && !isnan(tracerStartPointScreenActual.y)) {
						ImDrawList* drawList = ImGui::GetForegroundDrawList();
						float fovForW2S = camSettings.FOV;
						ImVec2 clipMin = ImVec2(0.0f, 0.0f);
						ImVec2 clipMax = ImVec2(screenSize.x, screenSize.y);

						SDK::FVectorData camAxisX, camAxisY, camAxisZ;
						GetAxes(camRotation, camAxisX, camAxisY, camAxisZ);

						for (const auto& opponentCar : allCars) {
							if (!opponentCar.IsValid()) continue;
							if (localPlayerActualCar.IsValid() && opponentCar.Address == localPlayerActualCar.Address) continue;

							SDK::FVectorData opponentCarWorldLocation = opponentCar.GetLocation(g_pRLSDK->GetMemoryManager());
							ImU32 currentTracerColor = defaultTracersColor;

							if (g_UseTeamColorsForTracers) {
								SDK::APRI opponentCarPRI = opponentCar.GetPRI(g_pRLSDK->GetMemoryManager());
								if (opponentCarPRI.IsValid()) {
									SDK::ATeamInfo teamInfo = opponentCarPRI.GetTeamInfo(g_pRLSDK->GetMemoryManager());
									if (teamInfo.IsValid()) {
										int32_t teamIndex = teamInfo.GetIndex(g_pRLSDK->GetMemoryManager());
										if (teamIndex == 0) { currentTracerColor = blueTeamColor; }
										else if (teamIndex == 1) { currentTracerColor = orangeTeamColor; }
									}
								}
							}

							ImVec2 p1_for_clip;
							bool targetConsideredInFront = false;

							std::optional<std::tuple<ImVec2, float>> opponentScreenPosOpt = WorldToScreen(
								opponentCarWorldLocation, camLocation, camRotation, fovForW2S, screenSize.x, screenSize.y);

							if (opponentScreenPosOpt.has_value()) {
								p1_for_clip = std::get<0>(opponentScreenPosOpt.value());
								targetConsideredInFront = true;
							}
							else {
								SDK::FVectorData world_dir_to_target = VectorSubtract(opponentCarWorldLocation, camLocation);

								float screen_space_dir_x = VectorDotProduct(world_dir_to_target, camAxisY);
								float screen_space_dir_y = VectorDotProduct(world_dir_to_target, camAxisZ);

								float len_sq = screen_space_dir_x * screen_space_dir_x + screen_space_dir_y * screen_space_dir_y;
								if (len_sq < 0.0001f) {
									screen_space_dir_x = 0.0f;
									screen_space_dir_y = -1.0f; 
								}
								else {
									float len = sqrt(len_sq);
									screen_space_dir_x /= len;
									screen_space_dir_y /= len;
								}

								float far_distance = screenSize.x + screenSize.y; 
								p1_for_clip.x = tracerStartPointScreenActual.x + screen_space_dir_x * far_distance;
								p1_for_clip.y = tracerStartPointScreenActual.y - screen_space_dir_y * far_distance;
								targetConsideredInFront = false;
							}

							ImVec2 p0_clipped = tracerStartPointScreenActual;
							ImVec2 p1_clipped = p1_for_clip;                

							if (ClipLineSegment(p0_clipped, p1_clipped, clipMin, clipMax)) {
								bool start_is_still_at_local_car_on_screen = (
									fabs(p0_clipped.x - tracerStartPointScreenActual.x) < 1.0f &&
									fabs(p0_clipped.y - tracerStartPointScreenActual.y) < 1.0f
									);

								if (start_is_still_at_local_car_on_screen) {
									drawList->AddLine(p0_clipped, p1_clipped, currentTracerColor, tracerThickness);
								}
							}
						}
					}
				}

				if (g_DrawPlayerDistanceText) {
					auto allCars = ge.GetCars(g_pRLSDK->GetMemoryManager());
					auto localPlayerControllers = ge.GetLocalPlayers(g_pRLSDK->GetMemoryManager());

					const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(g_ColorPlayerDistanceText);

					SDK::ACar localPlayerActualCar(0);
					uintptr_t localPlayerPRIAddress = 0;
					SDK::FVectorData localPlayerCarWorldLocation = { 0,0,0 }; 
					if (!localPlayerControllers.empty()) {
						SDK::APlayerController pc = localPlayerControllers[0];
						if (pc.IsValid()) {
							SDK::APRI localPRI = pc.GetPRI(g_pRLSDK->GetMemoryManager());
							if (localPRI.IsValid()) {
								localPlayerPRIAddress = localPRI.Address;
							}
						}
					}

					if (localPlayerPRIAddress == 0 && !allCars.empty()) {
						localPlayerActualCar = allCars[0];
						localPlayerCarWorldLocation = localPlayerActualCar.GetLocation(g_pRLSDK->GetMemoryManager());
					} else if (localPlayerPRIAddress != 0) {
						for (const auto& carInstance : allCars) {
							if (carInstance.IsValid()) {
								SDK::APRI carInstancePRI = carInstance.GetPRI(g_pRLSDK->GetMemoryManager());
								if (carInstancePRI.IsValid() && carInstancePRI.Address == localPlayerPRIAddress) {
									localPlayerActualCar = carInstance;
									localPlayerCarWorldLocation = localPlayerActualCar.GetLocation(g_pRLSDK->GetMemoryManager());
									break;
								}
							}
						}
					}

					if (localPlayerActualCar.IsValid()) {
						ImDrawList* drawList = ImGui::GetForegroundDrawList();
						float fovForW2S = camSettings.FOV;
						ImVec2 clipMin = ImVec2(0.0f, 0.0f);
						ImVec2 clipMax = ImVec2(screenSize.x, screenSize.y);

						SDK::FVectorData camAxisX, camAxisY, camAxisZ; 
						GetAxes(camRotation, camAxisX, camAxisY, camAxisZ);

						char distBuffer[64];

						for (const auto& opponentCar : allCars) {
							if (!opponentCar.IsValid()) continue;
							if (opponentCar.Address == localPlayerActualCar.Address) continue; 

							SDK::FVectorData opponentCarWorldLocation = opponentCar.GetLocation(g_pRLSDK->GetMemoryManager());

							SDK::FVectorData deltaVec = VectorSubtract(opponentCarWorldLocation, localPlayerCarWorldLocation);
							float distanceUU = VectorSize(deltaVec);
							float distanceMeters = distanceUU / 100.0f; 

							snprintf(distBuffer, sizeof(distBuffer), "%.1fm", distanceMeters);
							ImVec2 textSize = ImGui::CalcTextSize(distBuffer);

							ImVec2 textScreenPos = ImVec2(NAN, NAN);
							bool targetIsOnScreen = false;

							SDK::FVectorData opponentTextAnchorWorld = opponentCarWorldLocation;
							opponentTextAnchorWorld.Z += 50.0f; 

							std::optional<std::tuple<ImVec2, float>> opponentScreenPosOpt = WorldToScreen(
								opponentTextAnchorWorld, 
								camLocation, camRotation, fovForW2S, screenSize.x, screenSize.y);

							if (opponentScreenPosOpt.has_value()) {
								ImVec2 rawScreenPos = std::get<0>(opponentScreenPosOpt.value());
								if (rawScreenPos.x >= clipMin.x && rawScreenPos.x <= clipMax.x &&
									rawScreenPos.y >= clipMin.y && rawScreenPos.y <= clipMax.y) {
									textScreenPos = ImVec2(rawScreenPos.x - textSize.x, rawScreenPos.y - textSize.y);
									targetIsOnScreen = true;
								}
								else {
									textScreenPos = rawScreenPos;
								}
							}

							if (!targetIsOnScreen) {
								SDK::FVectorData world_dir_to_target_center = VectorSubtract(opponentCarWorldLocation, camLocation);

								float screen_space_dir_x = VectorDotProduct(world_dir_to_target_center, camAxisY);
								float screen_space_dir_y = VectorDotProduct(world_dir_to_target_center, camAxisZ);

								float len_sq = screen_space_dir_x * screen_space_dir_x + screen_space_dir_y * screen_space_dir_y;
								if (len_sq < 0.0001f) {
									screen_space_dir_x = 0.0f; screen_space_dir_y = -1.0f;
								}
								else {
									float len = sqrt(len_sq);
									screen_space_dir_x /= len; screen_space_dir_y /= len;
								}

								ImVec2 offscreen_origin = ImVec2(screenSize.x / 2.0f, screenSize.y / 2.0f);
								float far_distance = (screenSize.x + screenSize.y) / 2.0f;

								ImVec2 p1_offscreen_indicator_end;
								p1_offscreen_indicator_end.x = offscreen_origin.x + screen_space_dir_x * far_distance;
								p1_offscreen_indicator_end.y = offscreen_origin.y - screen_space_dir_y * far_distance;

								ImVec2 p0_clipped = offscreen_origin;
								ImVec2 p1_clipped = p1_offscreen_indicator_end;

								if (ClipLineSegment(p0_clipped, p1_clipped, clipMin, clipMax)) {
									ImVec2 edgePoint = p1_clipped;
									float padding = 5.0f;


									textScreenPos.x = edgePoint.x - textSize.x / 2.0f;
									textScreenPos.y = edgePoint.y - textSize.y / 2.0f;

									if (edgePoint.x <= clipMin.x + padding * 2) textScreenPos.x = clipMin.x + padding;
									else if (edgePoint.x >= clipMax.x - padding * 2) textScreenPos.x = clipMax.x - textSize.x - padding;

									if (edgePoint.y <= clipMin.y + padding * 2) textScreenPos.y = clipMin.y + padding; 
									else if (edgePoint.y >= clipMax.y - padding * 2) textScreenPos.y = clipMax.y - textSize.y - padding; 

								}
								else {
									textScreenPos = ImVec2(NAN, NAN); 
								}
							}

							if (!isnan(textScreenPos.x) && !isnan(textScreenPos.y)) {
								textScreenPos.x = (std::max)(clipMin.x, (std::min)(textScreenPos.x, clipMax.x - textSize.x));
								textScreenPos.y = (std::max)(clipMin.y, (std::min)(textScreenPos.y, clipMax.y - textSize.y));
								drawList->AddText(textScreenPos, textColor, distBuffer);
							}
						}
					}
				}

				if (g_DrawOpponentBoost) {
					auto cars = ge.GetCars(g_pRLSDK->GetMemoryManager());
					auto localPlayers = ge.GetLocalPlayers(g_pRLSDK->GetMemoryManager());
					const ImU32 boostCircleColor = ImGui::ColorConvertFloat4ToU32(g_ColorBoostCircle);

					uintptr_t localPlayerPRIAddr = 0;
					SDK::ACar localPlayerCar(0);
					
					if (!localPlayers.empty() && localPlayers[0].IsValid()) {
						SDK::APlayerController pc = localPlayers[0];
						SDK::APRI localPRI = pc.GetPRI(g_pRLSDK->GetMemoryManager());
						if (localPRI.IsValid()) {
							localPlayerPRIAddr = localPRI.Address;
						}
					}

					if (localPlayerPRIAddr == 0 && !cars.empty()) {
						localPlayerCar = cars[0];
					}

					for (const auto& car : cars) {
						if (!car.IsValid()) continue;

						if (localPlayerPRIAddr != 0) {
							SDK::APRI carPRI = car.GetPRI(g_pRLSDK->GetMemoryManager());
							if (carPRI.IsValid() && carPRI.Address == localPlayerPRIAddr) continue;
						} else if (localPlayerCar.IsValid() && car.Address == localPlayerCar.Address) {
							continue; 
						}

						SDK::UBoostComponent boostComp = car.GetBoostComponent(g_pRLSDK->GetMemoryManager());
						if (!boostComp.IsValid()) continue;

						float boostAmount = boostComp.GetAmount(g_pRLSDK->GetMemoryManager());
						SDK::FVectorData carLocation = car.GetLocation(g_pRLSDK->GetMemoryManager());

						carLocation.Z += 120.0f;

						SDK::APRI carPRI = car.GetPRI(g_pRLSDK->GetMemoryManager());

						DrawBoostCircle(carLocation, boostAmount, camLocation, camRotation, camSettings.FOV,
							screenSize.x, screenSize.y, boostCircleColor, g_pRLSDK->GetMemoryManager(), carPRI, g_UseTeamColorsForBoost);
					}
				}

				if (g_DrawVelocityPointers) {
					const float arrowScale = 150.0f;
					const float arrowHeadLength = 20.0f;
					const float arrowHeadWidth = 10.0f;
					const ImU32 ballArrowColor = ImGui::ColorConvertFloat4ToU32(g_ColorVelocityArrowBall);
					const ImU32 carArrowColor = ImGui::ColorConvertFloat4ToU32(g_ColorVelocityArrowCar);

					if (ball.IsValid()) { 
						DrawVelocityArrow3D(ballLoc, ballVel, arrowScale, arrowHeadLength, arrowHeadWidth, ballArrowColor,
							camLocation, camRotation, camSettings.FOV, screenSize.x, screenSize.y);
					}

					auto cars = ge.GetCars(g_pRLSDK->GetMemoryManager()); 
					

					uintptr_t localPlayerPRIAddr = 0;
					SDK::ACar localPlayerCar(0);
					
					if (!localPlayers.empty() && localPlayers[0].IsValid()) {
						SDK::APRI localPRI = localPlayers[0].GetPRI(g_pRLSDK->GetMemoryManager());
						if (localPRI.IsValid()) {
							localPlayerPRIAddr = localPRI.Address;
						}
					}
					
					if (localPlayerPRIAddr == 0 && !cars.empty()) {
						localPlayerCar = cars[0];
					}
					
					for (const auto& car : cars) {
						if (!car.IsValid()) continue;
						
						if (localPlayerPRIAddr != 0) {
							SDK::APRI carPRI = car.GetPRI(g_pRLSDK->GetMemoryManager());
							if (carPRI.IsValid() && carPRI.Address == localPlayerPRIAddr) continue;
						} else if (localPlayerCar.IsValid() && car.Address == localPlayerCar.Address) {
							continue;
						}
						
						SDK::FVectorData carLocation = car.GetLocation(g_pRLSDK->GetMemoryManager());
						SDK::FVectorData carVelocity = car.GetVelocity(g_pRLSDK->GetMemoryManager());
						DrawVelocityArrow3D(carLocation, carVelocity, arrowScale, arrowHeadLength, arrowHeadWidth, carArrowColor,
							camLocation, camRotation, camSettings.FOV, screenSize.x, screenSize.y);
					}
				}

				if (show && g_ShowDebugDrawingInfo) {
					ImGui::Separator();
					ImGui::Text("Camera Info (Live):");
					ImGui::Text("  Location: X=%.1f, Y=%.1f, Z=%.1f", camLocation.X, camLocation.Y, camLocation.Z);
					ImGui::Text("  Rotation: P=%d, Y=%d, R=%d", camRotation.Pitch, camRotation.Yaw, camRotation.Roll);
					ImGui::Text("  FOV (from PRI Settings): %.1f", camSettings.FOV);

					if (ballScreenPosOpt && ballOnScreen) {
						ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
							"Ball screen pos: (%.1f, %.1f) Depth: %.1f Transformed.Z: %.1f",
							ballScreenPos.x, ballScreenPos.y, ballTransformedZ, ballTransformedZ);
					}
					else if (ballScreenPosOpt) {
						ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
							"Ball off screen: (%.1f, %.1f) Depth: %.1f Transformed.Z: %.1f",
							ballScreenPos.x, ballScreenPos.y, ballTransformedZ, ballTransformedZ);
					}
					else {
						ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
							"Ball projection failed (Behind camera?) Transformed.Z: %.1f", ballTransformedZ);
					}
				}

			}
			catch (const std::exception& e) {
				if (show) { 
					Logger::Error("Ball ESP Drawing Exception: " + std::string(e.what()));
					ImGui::Separator(); 
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Ball ESP Error: %s", e.what());
				}
				else { 
					
				}
			}
			catch (...) { 
				if (show) {
					Logger::Error("Ball ESP Drawing: Unknown exception occurred.");
					ImGui::Separator();
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Ball ESP Error: Unknown exception");
				}
			}
		}
	}

	if (show) {
		ImGui::SetNextWindowSize(ImVec2(900, 450), ImGuiCond_FirstUseEver);
		ImGui::Begin(("VitriumBot [Made by tntgamer0815 | needlesspage819]"), &show, ImGuiWindowFlags_NoCollapse);

		if (ImGui::BeginTabBar("MainTabBar")) {

			if (ImGui::BeginTabItem("SDK Status")) {
				ImGui::Text("SDK Status:");
				ImGui::SameLine();
				if (g_SdkAndHooksInitialized.load()) { 
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Initialized Successfully");
					if (g_pRLSDK) ImGui::TextDisabled(" (%s)", g_pRLSDK->GetBuildType().c_str());
				}
				else {
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Initialization Failed");
					ImGui::TextWrapped("Error: SDK/Hooks failed to initialize (Check log.txt)."); 
				}
				ImGui::Separator();

				ImGui::Text("Game Info:"); 
				if (g_SdkAndHooksInitialized && g_pRLSDK) {
					SDK::AGameEvent ge_debug = g_pRLSDK->GetCurrentGameEvent(); 
					if (ge_debug.IsValid()) {
						ImGui::Text("GameEvent Found: %s", Logger::to_hex(ge_debug.Address).c_str());
						ImGui::Separator();
						ImGui::Text("Ball Info:");

						auto balls_debug = ge_debug.GetBalls(g_pRLSDK->GetMemoryManager());
						if (!balls_debug.empty()) {
							SDK::ABall ball_debug = balls_debug[0];
							if (ball_debug.IsValid()) {
								SDK::FVectorData ballLoc_debug = ball_debug.GetLocation(g_pRLSDK->GetMemoryManager());
								SDK::FVectorData ballVel_debug = ball_debug.GetVelocity(g_pRLSDK->GetMemoryManager());
								ImGui::Text("  Ball Loc: X=%.1f, Y=%.1f, Z=%.1f", ballLoc_debug.X, ballLoc_debug.Y, ballLoc_debug.Z);
								ImGui::Text("  Ball Vel: X=%.1f, Y=%.1f, Z=%.1f", ballVel_debug.X, ballVel_debug.Y, ballVel_debug.Z);
							}
							else {
								ImGui::Text("  Ball object pointer invalid.");
							}
						}
						else {
							ImGui::Text("  No balls found in GameEvent array.");
						}

						ImGui::Separator();
						ImGui::Text("Car Info:");
						auto cars_debug = ge_debug.GetCars(g_pRLSDK->GetMemoryManager());
						if (!cars_debug.empty()) {
							for (size_t i = 0; i < cars_debug.size(); ++i) {
								SDK::ACar car_debug = cars_debug[i];
								if (car_debug.IsValid()) {
									std::wstring playerName_debug = L"";
									SDK::APRI pri_debug = car_debug.GetPRI(g_pRLSDK->GetMemoryManager());
									if (pri_debug.IsValid()) {
										playerName_debug = pri_debug.GetPlayerName(g_pRLSDK->GetMemoryManager());
									}
									std::string nameStr_debug = (playerName_debug.empty() ? "Car[" + std::to_string(i) + "]" : std::string(playerName_debug.begin(), playerName_debug.end()));

									SDK::FVectorData carLoc_debug = car_debug.GetLocation(g_pRLSDK->GetMemoryManager());
									SDK::FVectorData carVel_debug = car_debug.GetVelocity(g_pRLSDK->GetMemoryManager());
									ImGui::Text("  %s:", nameStr_debug.c_str());
									ImGui::Text("    Loc: X=%.1f, Y=%.1f, Z=%.1f", carLoc_debug.X, carLoc_debug.Y, carLoc_debug.Z);
									ImGui::Text("    Vel: X=%.1f, Y=%.1f, Z=%.1f", carVel_debug.X, carVel_debug.Y, carVel_debug.Z);
								}
							}
						}
						else {
							ImGui::Text("  No cars found in GameEvent array.");
						}

					}
					else {
						ImGui::Text("GameEvent Not Found (Not in match?)");
					}
				}
				else {
					ImGui::Text("SDK Not Initialized.");
				}

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Visuals")) {
				ImGui::Text("Drawing Options:");
				ImGui::Checkbox("Draw Ball (2D Circle)", &g_DrawBallCircle);
				ImGui::SameLine(); HelpMarker("Draws a simple 2D circle around the ball.");
				ImGui::SameLine(); ImGui::ColorEdit4("##ColorCircle2D", (float*)&g_ColorBallCircle2D, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

				ImGui::Checkbox("Draw Ball (3D Sphere)", &g_Draw3DSphere);
				ImGui::SameLine(); HelpMarker("Draws a wireframe sphere around the ball. Overrides 2D Circle if both are checked.");
				ImGui::SameLine(); ImGui::ColorEdit4("##ColorSphere3D", (float*)&g_ColorBallSphere3D, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

				ImGui::Checkbox("Draw Ball Prediction", &g_DrawBallPrediction);
				ImGui::SameLine(); HelpMarker("Draws a line indicating the ball's predicted path.");

				ImGui::Checkbox("Draw Car Hitboxes (Octane)", &g_DrawCarHitboxes);
				ImGui::SameLine(); HelpMarker("Draws a wireframe box representing the car's hitbox (currently uses Octane dimensions for all).");
				ImGui::SameLine(); ImGui::ColorEdit4("##ColorHitbox", (float*)&g_ColorCarHitbox, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

				ImGui::Checkbox("Draw Car Distance", &g_DrawPlayerDistanceText);
				ImGui::SameLine(); HelpMarker("Draws text to distance of the cars.");
				ImGui::SameLine(); ImGui::ColorEdit4("##ColorPlayerDistanceText", (float*)&g_ColorPlayerDistanceText, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

				ImGui::Checkbox("Draw Velocity Pointers", &g_DrawVelocityPointers);
				ImGui::SameLine(); HelpMarker("Draws an arrow indicating the direction and magnitude of velocity for cars and the ball.");

				ImGui::Indent();
				ImGui::Text("Ball Arrow Color:"); ImGui::SameLine();
				ImGui::ColorEdit4("##ColorVelBall", (float*)&g_ColorVelocityArrowBall, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
				ImGui::Text("Car Arrow Color:"); ImGui::SameLine();
				ImGui::ColorEdit4("##ColorVelCar", (float*)&g_ColorVelocityArrowCar, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
				ImGui::Unindent();


				ImGui::Checkbox("Draw Boost Indicators", &g_DrawOpponentBoost);
				ImGui::SameLine(); HelpMarker("Shows a circular indicator of boost amount above opponents' cars.");
				ImGui::SameLine(); ImGui::ColorEdit4("##ColorBoostCircle", (float*)&g_ColorBoostCircle, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

				ImGui::Indent();
				ImGui::Checkbox("Use Team Colors For Boost Indicators", &g_UseTeamColorsForBoost);
				ImGui::SameLine(); HelpMarker("Uses player team colors for boost circles instead of the default color.");
				ImGui::Unindent();

				ImGui::Checkbox("Draw Tracers", &g_DrawTracers);
				ImGui::SameLine(); HelpMarker("Shows lines to the cars.");
				ImGui::SameLine(); ImGui::ColorEdit4("##ColorTracers", (float*)&g_ColorTracers, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);

				if (g_DrawTracers) {
					ImGui::Indent();
					ImGui::Checkbox("Use Team Colors For Tracers", &g_UseTeamColorsForTracers);
					ImGui::Unindent();
				}

				ImGui::Separator();
				ImGui::Text("Debug Overlays:");
				if (ImGui::Checkbox("Show Live Debug Info", &g_ShowDebugDrawingInfo)) g_SettingsChangedForClient = true;
				ImGui::SameLine(); HelpMarker("Shows live camera and screen position info in the menu when active.");

				ImGui::Checkbox("Draw Boost Pad Timers", &g_DrawBoostPadTimers);
				ImGui::SameLine(); HelpMarker("Shows countdown timers for boost pads when they're picked up.");

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Bot Settings (Client)")) {
				ImGui::Text("Controls for the Python bot client (only_nexto.py).");
				ImGui::Separator();

				bool tempBotEnabled = g_BotEnabledForClient.load();
				if (ImGui::Checkbox("Enable Bot (Client)", &tempBotEnabled)) {
					g_BotEnabledForClient.store(tempBotEnabled);
					{
						std::lock_guard<std::mutex> lock(g_SettingsMutex);
						g_SettingsChangedForClient = true;
					}
				}
				ImGui::SameLine(); HelpMarker("Toggles the bot logic in the Client.");

				const char* bot_display_names[] = { "Nexto", "NextMortal", "Genesis", "Unstable", "Element", "Carbon", "Karma (Platin)", "The Bog V6", "necto" };
				const char* bot_server_names[] = { "Nexto", "NextMortal", "Genesis", "unstable", "Element", "Carbon", "karma", "thebog", "necto" };
				static int current_bot_idx = 0;
				std::string current_selected_bot_local;
				{
					std::lock_guard<std::mutex> lock(g_SettingsMutex);
					current_selected_bot_local = g_SelectedBotNameForClient;
				}
				for (int i = 0; i < IM_ARRAYSIZE(bot_server_names); ++i) {
					if (current_selected_bot_local == bot_server_names[i]) {
						current_bot_idx = i;
						break;
					}
				}

				if (ImGui::Combo("Selected Bot", &current_bot_idx, bot_display_names, IM_ARRAYSIZE(bot_display_names))) {
					{
						std::lock_guard<std::mutex> lock(g_SettingsMutex);
						g_SelectedBotNameForClient = bot_server_names[current_bot_idx];
						g_SettingsChangedForClient = true;
					}
				}
				ImGui::SameLine(); HelpMarker("Select the bot to be used by the Client.");

				ImGui::Spacing();

				ImGui::Text("Bot Toggle Key: %s", g_BotToggleKeyName.c_str());
				ImGui::SameLine();
				if (g_IsSettingBotToggleKey.load()) {
					ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press a key... (ESC to cancel)");
				}
				else {
					if (ImGui::Button("Set Toggle Key")) {
						g_IsSettingBotToggleKey.store(true);
					}
				}
				ImGui::Spacing();

				bool tempAutoBallCam = g_AutoBallCamForClient.load();
				if (ImGui::Checkbox("Enable Auto Ball Cam (Python)", &tempAutoBallCam)) {
					g_AutoBallCamForClient.store(tempAutoBallCam);
					g_SettingsChangedForClient = true;
				}
				ImGui::SameLine(); HelpMarker("Toggles auto ball cam in the Client.");

				bool tempSpeedflipKickoff = g_SpeedflipKickoffForClient.load();
				if (ImGui::Checkbox("Enable Speedflip Kickoff (Python)", &tempSpeedflipKickoff)) {
					g_SpeedflipKickoffForClient.store(tempSpeedflipKickoff);
					g_SettingsChangedForClient = true;
				}
				ImGui::SameLine(); HelpMarker("Toggles speedflip kickoff in the Client.");

				ImGui::Separator();

				bool tempPythonMonitoring = false;
				g_PythonMonitoringForClient.store(false);

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Connection Status")) {
				ImGui::Text("Server & Client Connection Information");
				ImGui::Separator();

				ImGui::Text("Client Connected: ");
				ImGui::SameLine();
				if (g_ClientConnected.load()) {
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "YES");
				}
				else {
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "NO");
				}

				auto now = std::chrono::steady_clock::now();
				auto uptime_duration = std::chrono::duration_cast<std::chrono::seconds>(now - g_ServerStartTime);
				long long uptime_s = uptime_duration.count();
				ImGui::Text("Server Uptime: %lldh %lldm %llds", uptime_s / 3600, (uptime_s % 3600) / 60, uptime_s % 60);

				ImGui::Text("Total Connections Established: %llu", g_ConnectionsEstablished.load());
				ImGui::Separator();
				ImGui::Text("Data Sent to Client:");
				ImGui::Text("  Packets Sent: %llu", g_PacketsSentToClient.load());
				ImGui::Text("  Bytes Sent: %llu bytes", g_BytesSentToClient.load());

				if (g_LastPacketSentTimeValid.load()) {
					auto last_sent_duration = std::chrono::duration_cast<std::chrono::seconds>(now - g_LastPacketSentTime);
					ImGui::Text("  Time Since Last Packet Sent: %llds ago", last_sent_duration.count());
				}
				else {
					ImGui::Text("  Time Since Last Packet Sent: N/A (no packets sent yet or client disconnected)");
				}

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Misc")) {
				ImGui::Text("DLL Management:");
				if (ImGui::Button("Eject DLL")) {
					Logger::Info("Eject button clicked. Shutting down Kiero and preparing to unload DLL.");
					kiero::shutdown(); 
					uintptr_t hThread = _beginthreadex(NULL, 0, EjectThread, reinterpret_cast<void*>(g_hModule), 0, NULL);
					if (hThread != 0) {
						CloseHandle(reinterpret_cast<HANDLE>(hThread));
					}
					else {
						Logger::Error("Failed to create EjectThread!");
					}
				}
				ImGui::SameLine();
				ImGui::TextDisabled("(Unloads the DLL)");

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
		ImGui::End();
	}

	ImGui::Render();

	if (mainRenderTargetView) {
		pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}
	else {
		ID3D11Texture2D* pBackBuffer = nullptr;
		if (pDevice != nullptr && SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer)) && pBackBuffer != nullptr) {
			if (SUCCEEDED(pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView)) && mainRenderTargetView != nullptr) {
				pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			}
			else {
				Logger::Error("hkPresent Error: Failed to recreate RenderTargetView.");
				if (mainRenderTargetView) { mainRenderTargetView->Release(); mainRenderTargetView = nullptr; }
			}
			pBackBuffer->Release();
		}
		else {
			if (pDevice) Logger::Error("hkPresent Error: Failed to get back buffer for RTV recreation.");
		}
	}

	return oPresent(pSwapChain, SyncInterval, Flags);
}

bool InitializeSDKAndHooks() {
	Logger::Info("InitializeSDKAndHooks: Attempting Kiero D3D11 init (this also initializes MinHook)...");
	if (kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success) {
		Logger::Error("InitializeSDKAndHooks: kiero::init FAILED. Cannot proceed.");
		return false;
	}
	Logger::Info("InitializeSDKAndHooks: Kiero D3D11 init (and MinHook) SUCCESSFUL.");

	Logger::Info("InitializeSDKAndHooks: Attempting to initialize RLSDK...");
	try {
		g_pRLSDK = std::make_unique<RLSDK>(DEFAULT_PROCESS_NAME, false); //my hooks sadly dont work, and i cant figure out why.

		if (!g_pRLSDK || !g_pRLSDK->IsInitialized()) {
			Logger::Error("InitializeSDKAndHooks: RLSDK make_unique succeeded but RLSDK internal initialization failed.");
			kiero::shutdown();
			return false;
		}

		if (g_pRLSDK) {
			g_pRLSDK->Subscribe(EventType::OnRoundActiveStateChanged, [](const EventData& data) {
				if (const auto* stateData = dynamic_cast<const EventRoundActiveStateChangedData*>(&data)) {
					Logger::Info("[EVENT] Round Active State Changed: " + std::string(stateData->IsActive ? "true" : "false"));
				}
				});
			g_pRLSDK->Subscribe(EventType::OnKeyPressed, [](const EventData& data) {
				if (const auto* keyData = dynamic_cast<const EventKeyPressedData*>(&data)) {
					Logger::Info("[EVENT] Key: " + keyData->KeyName + " Type: " + std::to_string(static_cast<int>(keyData->EventType)));
				}
				});
			g_pRLSDK->Subscribe(EventType::OnBoostPadStateChanged, [](const EventData& data) {
				if (const auto* boostData = dynamic_cast<const EventBoostPadChangedData*>(&data)) {
					Logger::Info("[EVENT] Boost Pad " + Logger::to_hex(boostData->BoostPickupAddress) + " Active: " + (boostData->IsNowActive ? "true" : "false"));
					
					if (g_pRLSDK) {
						auto* pad = g_FieldState.FindPadByActorAddress(boostData->BoostPickupAddress);
						if (pad) {
							if (boostData->IsNowActive) {
								pad->Reset();
							} else {
								pad->MarkInactive();
							}
						}
					}
				}
			});
			
			g_pRLSDK->Subscribe(EventType::OnResetPickups, [](const EventData& data) {
				Logger::Info("[EVENT] Reset Pickups");
				g_FieldState.ResetBoostPads();
			});
			
			g_FieldState.ResetBoostPads();
			Logger::Info("InitializeSDKAndHooks: FieldState initialized with boost pads.");
		}
		else {
			throw std::runtime_error("RLSDK pointer is null after construction (should not happen).");
		}

	}
	catch (const std::exception& e) {
		Logger::Error("InitializeSDKAndHooks: RLSDK Initialization caught exception: " + std::string(e.what()));
		g_pRLSDK = nullptr;
		kiero::shutdown();
		g_SdkAndHooksInitialized.store(false);
		return false;
	}
	catch (...) {
		Logger::Error("InitializeSDKAndHooks: RLSDK Initialization failed with an unknown exception.");
		g_pRLSDK = nullptr;
		kiero::shutdown();
		g_SdkAndHooksInitialized.store(false);
		return false;
	}

	Logger::Info("InitializeSDKAndHooks: RLSDK Initialized. Attempting Kiero Present bind...");
	if (kiero::bind(8, (void**)&oPresent, hkPresent) == kiero::Status::Success) {
		Logger::Info("InitializeSDKAndHooks: Kiero Present Hooked successfully.");
		init_hook = true;
		g_SdkAndHooksInitialized.store(true);
		return true;
	}
	else {
		Logger::Error("InitializeSDKAndHooks: kiero::bind Present FAILED.");
		if (g_pRLSDK) g_pRLSDK.reset(); 
		kiero::shutdown();
		g_SdkAndHooksInitialized.store(false); 
		return false;
	}
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
	OutputDebugStringA("MainThread: Entered.\n");
	if (!Logger::Initialize()) { 
		OutputDebugStringA("MainThread: Logger::Initialize() FAILED. Check DebugView++ and permissions.\n");
		MessageBoxA(NULL, "Logger initialization failed! Cannot continue.", "Critical Error", MB_OK | MB_ICONERROR);
		return 1;
	}
	else {
		OutputDebugStringA("MainThread: Logger::Initialize() SUCCEEDED.\n");
		Logger::Info("MainThread: Starting System initialization..."); 
	}

	Logger::Info("MainThread: Starting server thread...");
	g_hServerMainLoopThread = (HANDLE)_beginthreadex(NULL, 0, ServerMainLoopThread, NULL, 0, NULL);
	if (g_hServerMainLoopThread == NULL) {
		Logger::Error("MainThread: Failed to create ServerMainLoopThread!");
		Logger::Shutdown();
		return 1;
	}
	Logger::Info("MainThread: Server thread created successfully.");

	Logger::Info("MainThread: Setup complete. Waiting for shutdown signal...");
	while (!g_ShutdownSignal.load()) { 
		Sleep(100); 
	}

	Logger::Info("MainThread: Shutdown signal received. Cleaning up...");
	Logger::Shutdown();
	return 0;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hMod);
		g_hModule = hMod;
		if (HANDLE hThread = CreateThread(nullptr, 0, MainThread, hMod, 0, nullptr)) {
			CloseHandle(hThread); 
		}
		else {
			MessageBoxA(NULL, "Failed to create initialization thread!", "DLL Attach Error", MB_OK | MB_ICONERROR);
			return FALSE;
		}
		break;
	case DLL_PROCESS_DETACH:
		Logger::Info("DLL Detaching...");
		g_ShutdownSignal = true;

		if (g_ServerThreadActive) {
			Logger::Info("DLL_PROCESS_DETACH: Stopping server thread...");
			g_ServerThreadActive = false;
			if (g_hServerMainLoopThread != NULL) {
				WaitForSingleObject(g_hServerMainLoopThread, 500); 
				CloseHandle(g_hServerMainLoopThread);
				g_hServerMainLoopThread = NULL;
			}
			Logger::Info("DLL_PROCESS_DETACH: Server thread hopefully stopped.");
		}

		if (g_pRLSDK) {
			g_pRLSDK.reset(); 
			Logger::Info("DLL_PROCESS_DETACH: RLSDK instance reset.");
		}

		if (g_ClientSocket != INVALID_SOCKET) {
			closesocket(g_ClientSocket);
			g_ClientSocket = INVALID_SOCKET;
			Logger::Info("DLL_PROCESS_DETACH: Client socket closed.");
		}
		if (g_ServerSocket != INVALID_SOCKET) {
			closesocket(g_ServerSocket);
			g_ServerSocket = INVALID_SOCKET;
			Logger::Info("DLL_PROCESS_DETACH: Server socket closed.");
		}

		WSACleanup();
		Logger::Info("DLL_PROCESS_DETACH: Winsock cleaned up.");

		kiero::shutdown();
		Logger::Info("Cleanup complete.");
		break;
	}
	return TRUE;
}

std::string VirtualKeyToString(UINT vkCode) {
	if (vkCode >= VK_F1 && vkCode <= VK_F12) {
		return "F" + std::to_string(vkCode - VK_F1 + 1);
	}
	if (vkCode >= '0' && vkCode <= '9') {
		return std::string(1, static_cast<char>(vkCode));
	}
	if (vkCode >= 'A' && vkCode <= 'Z') {
		return std::string(1, static_cast<char>(vkCode));
	}
	switch (vkCode) {
	case VK_SPACE: return "SPACE";
	case VK_RETURN: return "ENTER";
	case VK_INSERT: return "INSERT";
	case VK_DELETE: return "DELETE";
	case VK_HOME: return "HOME";
	case VK_END: return "END";
	case VK_PRIOR: return "PAGE_UP";
	case VK_NEXT: return "PAGE_DOWN";
	case VK_LEFT: return "LEFT_ARROW";
	case VK_RIGHT: return "RIGHT_ARROW";
	case VK_UP: return "UP_ARROW";
	case VK_DOWN: return "DOWN_ARROW";
	case VK_SHIFT: return "SHIFT";
	case VK_CONTROL: return "CTRL";
	case VK_MENU: return "ALT"; 
	default: return "VK_0x" + Logger::to_hex(vkCode); 
	}
}