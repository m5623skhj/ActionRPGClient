#include <Windows.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace
{
    constexpr int PANEL_WIDTH = 230;
    constexpr float MIN_ZOOM = 0.02f;
    constexpr float MAX_ZOOM = 16.0f;

    enum ControlId
    {
        ADD_XY = 100,
        ADD_RIGHT,
        APPLY_POSITION,
        POSITION_X,
        POSITION_Y,
        MODE_SELECT,
        MODE_WALKABLE,
        MODE_BLOCKED,
        MODE_SPAWN,
        CLEAR_AREAS,
        FIT_MAP,
        SAVE_MAP
    };

    enum class EditMode { Select, Walkable, Blocked, Spawn };

    struct FloatPoint { float x{}; float y{}; };
    using Polygon = std::vector<FloatPoint>;

    struct MapImage
    {
        std::string asset;
        float x{};
        float y{};
        float width{};
        float height{};
    };

    struct Bounds { float left{}; float top{}; float right{}; float bottom{}; };

    struct TownMapDocument
    {
        std::string mapId = "town_01";
        Bounds world{ 0.0f, 0.0f, 2400.0f, 1400.0f };
        std::vector<MapImage> images;
        std::vector<Polygon> walkablePolygons{
            { { 0.0f, 360.0f }, { 2400.0f, 360.0f }, { 2400.0f, 1400.0f }, { 0.0f, 1400.0f } }
        };
        std::vector<Polygon> blockedPolygons;
        FloatPoint spawn{ 640.0f, 640.0f };
        float sectorSize = 480.0f;
        float walkSpeed = 280.0f;
        float runSpeed = 480.0f;
    };

    struct CachedImage
    {
        std::unique_ptr<Gdiplus::Image> image;
        std::uint64_t lastUsedFrame{};
        bool loadFailed{};
    };

    std::wstring Utf8ToWide(const std::string& inText)
    {
        if (inText.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            inText.data(), static_cast<int>(inText.size()), nullptr, 0);
        if (length <= 0) throw std::runtime_error("Invalid UTF-8 path.");
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, inText.data(),
            static_cast<int>(inText.size()), result.data(), length);
        return result;
    }

    std::string WideToUtf8(const std::wstring& inText)
    {
        if (inText.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            inText.data(), static_cast<int>(inText.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) throw std::runtime_error("Invalid UTF-16 path.");
        std::string result(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, inText.data(),
            static_cast<int>(inText.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    std::filesystem::path GetExecutableDirectory()
    {
        std::vector<wchar_t> buffer(MAX_PATH);
        while (true)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) throw std::runtime_error("GetModuleFileNameW failed.");
            if (length < buffer.size() - 1)
                return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
            buffer.resize(buffer.size() * 2);
        }
    }

    std::filesystem::path FindDefaultAssetRoot()
    {
        std::filesystem::path current = GetExecutableDirectory();
        std::filesystem::path found;
        for (int depth = 0; depth < 8; ++depth)
        {
            const std::filesystem::path candidate = current / L"Assets";
            if (std::filesystem::is_regular_file(candidate / L"Data" / L"assets.ini")) found = candidate;
            if (!current.has_parent_path()) break;
            current = current.parent_path();
        }
        if (!found.empty()) return found;
        return GetExecutableDirectory() / L"Assets";
    }

    bool IsPointOnSegment(const FloatPoint inPoint, const FloatPoint inStart, const FloatPoint inEnd)
    {
        constexpr float EPSILON = 0.001f;
        const float dx = inEnd.x - inStart.x;
        const float dy = inEnd.y - inStart.y;
        const float cross = (inPoint.x - inStart.x) * dy - (inPoint.y - inStart.y) * dx;
        if (std::abs(cross) > EPSILON) return false;
        const float dot = (inPoint.x - inStart.x) * dx + (inPoint.y - inStart.y) * dy;
        return dot >= -EPSILON && dot <= dx * dx + dy * dy + EPSILON;
    }

    bool IsPointInPolygon(const FloatPoint inPoint, const Polygon& inPolygon)
    {
        bool inside = false;
        for (std::size_t current = 0, previous = inPolygon.size() - 1;
            current < inPolygon.size(); previous = current++)
        {
            const FloatPoint& start = inPolygon[previous];
            const FloatPoint& end = inPolygon[current];
            if (IsPointOnSegment(inPoint, start, end)) return true;
            if ((start.y > inPoint.y) != (end.y > inPoint.y))
            {
                const float x = (end.x - start.x) * (inPoint.y - start.y) / (end.y - start.y) + start.x;
                if (inPoint.x < x) inside = !inside;
            }
        }
        return inside;
    }

    class Editor final
    {
    public:
        Editor(std::filesystem::path inMapPath, std::filesystem::path inAssetRoot);
        void CreateControls(HWND inWindow);
        void LayoutControls(HWND inWindow) const;
        void Paint(HWND inWindow, HDC inDeviceContext);
        void HandleCommand(HWND inWindow, int inCommand);
        void HandleKey(HWND inWindow, WPARAM inKey);
        void BeginLeft(HWND inWindow, int inX, int inY);
        void EndPointer();
        void BeginPan(HWND inWindow, int inX, int inY);
        void MoveMouse(HWND inWindow, int inX, int inY);
        void ZoomAt(HWND inWindow, int inX, int inY, int inWheelDelta);
        void PlaceSpawn(HWND inWindow, int inX, int inY);

    private:
        void Load();
        void Save(HWND inWindow);
        void AddImage(HWND inWindow, bool inPlaceRight);
        void ApplyPosition(HWND inWindow);
        void UpdatePositionControls() const;
        void FinishPolygon(HWND inWindow);
        void SetMode(EditMode inMode, HWND inWindow);
        void Fit(HWND inWindow);
        void DrawImages(Gdiplus::Graphics& inGraphics, int inCanvasWidth, int inCanvasHeight);
        void DrawPolygons(Gdiplus::Graphics& inGraphics, const std::vector<Polygon>& inPolygons,
            Gdiplus::Color inFill, Gdiplus::Color inOutline) const;
        void DrawOverlays(Gdiplus::Graphics& inGraphics) const;
        [[nodiscard]] Gdiplus::Image* GetCachedImage(const std::string& inAsset);
        void TrimImageCache();
        [[nodiscard]] Bounds CalculateBounds() const;
        [[nodiscard]] bool IsSpawnValid() const;
        [[nodiscard]] std::optional<std::size_t> HitTestImage(FloatPoint inPoint) const;
        [[nodiscard]] FloatPoint ScreenToWorld(int inX, int inY) const;
        [[nodiscard]] Gdiplus::PointF WorldPoint(FloatPoint inPoint) const;
        [[nodiscard]] Gdiplus::RectF WorldRectangle(float inX, float inY, float inWidth, float inHeight) const;
        [[nodiscard]] static bool IsCanvasPoint(HWND inWindow, int inX, int inY);
        [[nodiscard]] static float ReadPosition(HWND inEdit);
        [[nodiscard]] static std::vector<Polygon> ReadPolygons(const nlohmann::json& inPolygons);
        [[nodiscard]] static nlohmann::json WritePolygons(const std::vector<Polygon>& inPolygons);

        std::filesystem::path mapPath;
        std::filesystem::path assetRoot;
        TownMapDocument document;
        EditMode mode = EditMode::Select;
        std::optional<std::size_t> selectedImage;
        Polygon workingPolygon;
        FloatPoint imageDragOffset{};
        POINT lastMouse{};
        float cameraX{};
        float cameraY{};
        float zoom = 0.5f;
        bool draggingImage{};
        bool panning{};
        HWND positionX{};
        HWND positionY{};
        HWND xLabel{};
        HWND yLabel{};
        std::vector<HWND> buttons;
        std::unordered_map<std::string, CachedImage> imageCache;
        std::uint64_t frameNumber{};
    };

    Editor::Editor(std::filesystem::path inMapPath, std::filesystem::path inAssetRoot)
        : mapPath(std::move(inMapPath)), assetRoot(std::move(inAssetRoot))
    {
        Load();
    }

    std::vector<Polygon> Editor::ReadPolygons(const nlohmann::json& inPolygons)
    {
        std::vector<Polygon> result;
        for (const nlohmann::json& inputPolygon : inPolygons)
        {
            Polygon polygon;
            for (const nlohmann::json& point : inputPolygon)
                polygon.push_back({ point.at("x").get<float>(), point.at("y").get<float>() });
            result.push_back(std::move(polygon));
        }
        return result;
    }

    nlohmann::json Editor::WritePolygons(const std::vector<Polygon>& inPolygons)
    {
        nlohmann::json result = nlohmann::json::array();
        for (const Polygon& polygon : inPolygons)
        {
            nlohmann::json points = nlohmann::json::array();
            for (const FloatPoint point : polygon)
                points.push_back({ { "x", point.x }, { "y", point.y } });
            result.push_back(std::move(points));
        }
        return result;
    }

    void Editor::Load()
    {
        if (!std::filesystem::exists(mapPath)) return;
        std::ifstream stream(mapPath);
        nlohmann::json input;
        stream >> input;
        const int version = input.at("version").get<int>();
        document.mapId = input.at("mapId").get<std::string>();
        document.sectorSize = input.at("sectorSize").get<float>();
        document.walkSpeed = input.at("walkSpeed").get<float>();
        document.runSpeed = input.at("runSpeed").get<float>();
        document.spawn = { input.at("spawn").at("x").get<float>(), input.at("spawn").at("y").get<float>() };

        if (version == 1)
        {
            document.world = { 0.0f, 0.0f,
                input.at("world").at("width").get<float>(), input.at("world").at("height").get<float>() };
            const nlohmann::json& walkable = input.at("walkable");
            const float left = walkable.at("left").get<float>();
            const float top = walkable.at("top").get<float>();
            const float right = walkable.at("right").get<float>();
            const float bottom = walkable.at("bottom").get<float>();
            document.walkablePolygons = { { { left, top }, { right, top },
                { right, bottom }, { left, bottom } } };
            return;
        }
        if (version != 2) throw std::runtime_error("Unsupported map version.");

        document.world = {
            input.at("world").at("left").get<float>(), input.at("world").at("top").get<float>(),
            input.at("world").at("right").get<float>(), input.at("world").at("bottom").get<float>()
        };
        document.images.clear();
        for (const nlohmann::json& image : input.at("images"))
        {
            document.images.push_back(MapImage{
                image.at("asset").get<std::string>(), image.at("x").get<float>(),
                image.at("y").get<float>(), image.at("width").get<float>(),
                image.at("height").get<float>()
            });
        }
        document.walkablePolygons = ReadPolygons(input.at("walkablePolygons"));
        document.blockedPolygons = ReadPolygons(input.at("blockedPolygons"));
    }

    void Editor::Save(HWND inWindow)
    {
        if (!workingPolygon.empty())
        {
            MessageBoxW(inWindow, L"Finish or cancel the current polygon before saving.",
                L"Town Map Editor", MB_OK | MB_ICONWARNING);
            return;
        }
        if (document.walkablePolygons.empty() || !IsSpawnValid())
        {
            MessageBoxW(inWindow, L"A walkable polygon and a valid spawn are required.",
                L"Town Map Editor", MB_OK | MB_ICONWARNING);
            return;
        }
        std::size_t totalVertexCount{};
        for (const auto* polygons : { &document.walkablePolygons, &document.blockedPolygons })
            for (const Polygon& polygon : *polygons) totalVertexCount += polygon.size();
        if (totalVertexCount > 32768)
        {
            MessageBoxW(inWindow, L"The map exceeds the 32768 polygon vertex limit.",
                L"Town Map Editor", MB_OK | MB_ICONWARNING);
            return;
        }

        document.world = CalculateBounds();
        nlohmann::json output{
            { "version", 2 }, { "mapId", document.mapId },
            { "world", { { "left", document.world.left }, { "top", document.world.top },
                { "right", document.world.right }, { "bottom", document.world.bottom } } },
            { "images", nlohmann::json::array() },
            { "walkablePolygons", WritePolygons(document.walkablePolygons) },
            { "blockedPolygons", WritePolygons(document.blockedPolygons) },
            { "spawn", { { "x", document.spawn.x }, { "y", document.spawn.y } } },
            { "sectorSize", document.sectorSize }, { "walkSpeed", document.walkSpeed },
            { "runSpeed", document.runSpeed }
        };
        for (const MapImage& image : document.images)
        {
            output["images"].push_back({ { "asset", image.asset }, { "x", image.x },
                { "y", image.y }, { "width", image.width }, { "height", image.height } });
        }

        if (!mapPath.parent_path().empty()) std::filesystem::create_directories(mapPath.parent_path());
        std::ofstream stream(mapPath);
        if (!stream) throw std::runtime_error("Unable to save the town map.");
        stream << std::setw(2) << output << '\n';
        SetWindowTextW(inWindow, (L"Town Map Editor - saved: " + mapPath.wstring()).c_str());
    }

    void Editor::CreateControls(HWND inWindow)
    {
        const auto addButton = [this, inWindow](const wchar_t* inText, const int inId)
        {
            buttons.push_back(CreateWindowW(L"BUTTON", inText, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 100, 28, inWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(inId)),
                GetModuleHandleW(nullptr), nullptr));
        };
        addButton(L"Add at X/Y", ADD_XY);
        addButton(L"Add Right", ADD_RIGHT);
        xLabel = CreateWindowW(L"STATIC", L"X", WS_CHILD | WS_VISIBLE, 0, 0, 20, 22,
            inWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        yLabel = CreateWindowW(L"STATIC", L"Y", WS_CHILD | WS_VISIBLE, 0, 0, 20, 22,
            inWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        positionX = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 80, 24, inWindow, reinterpret_cast<HMENU>(POSITION_X), GetModuleHandleW(nullptr), nullptr);
        positionY = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 80, 24, inWindow, reinterpret_cast<HMENU>(POSITION_Y), GetModuleHandleW(nullptr), nullptr);
        addButton(L"Apply Position", APPLY_POSITION);
        addButton(L"V  Select", MODE_SELECT);
        addButton(L"W  Walkable", MODE_WALKABLE);
        addButton(L"B  Blocked", MODE_BLOCKED);
        addButton(L"P  Spawn", MODE_SPAWN);
        addButton(L"Clear Mode Areas", CLEAR_AREAS);
        addButton(L"F  Fit All", FIT_MAP);
        addButton(L"S  Save", SAVE_MAP);
        LayoutControls(inWindow);
    }

    void Editor::LayoutControls(HWND inWindow) const
    {
        RECT client{};
        GetClientRect(inWindow, &client);
        const int left = std::max(0L, client.right - PANEL_WIDTH) + 12;
        int y = 18;
        for (std::size_t index = 0; index < buttons.size(); ++index)
        {
            if (index == 2) y = 132;
            MoveWindow(buttons[index], left, y, PANEL_WIDTH - 24, 30, TRUE);
            y += 36;
        }
        MoveWindow(xLabel, left, 97, 16, 22, TRUE);
        MoveWindow(positionX, left + 18, 94, 78, 25, TRUE);
        MoveWindow(yLabel, left + 104, 97, 16, 22, TRUE);
        MoveWindow(positionY, left + 122, 94, 78, 25, TRUE);
    }

    void Editor::HandleCommand(HWND inWindow, const int inCommand)
    {
        switch (inCommand)
        {
        case ADD_XY: AddImage(inWindow, false); break;
        case ADD_RIGHT: AddImage(inWindow, true); break;
        case APPLY_POSITION: ApplyPosition(inWindow); break;
        case MODE_SELECT: SetMode(EditMode::Select, inWindow); break;
        case MODE_WALKABLE: SetMode(EditMode::Walkable, inWindow); break;
        case MODE_BLOCKED: SetMode(EditMode::Blocked, inWindow); break;
        case MODE_SPAWN: SetMode(EditMode::Spawn, inWindow); break;
        case CLEAR_AREAS:
            if (mode == EditMode::Walkable) document.walkablePolygons.clear();
            else if (mode == EditMode::Blocked) document.blockedPolygons.clear();
            workingPolygon.clear();
            InvalidateRect(inWindow, nullptr, FALSE);
            break;
        case FIT_MAP: Fit(inWindow); break;
        case SAVE_MAP: Save(inWindow); break;
        default: break;
        }
    }

    void Editor::HandleKey(HWND inWindow, const WPARAM inKey)
    {
        if (GetFocus() == positionX || GetFocus() == positionY)
        {
            if (inKey == VK_RETURN) ApplyPosition(inWindow);
            return;
        }
        if (inKey == 'V') SetMode(EditMode::Select, inWindow);
        else if (inKey == 'W') SetMode(EditMode::Walkable, inWindow);
        else if (inKey == 'B') SetMode(EditMode::Blocked, inWindow);
        else if (inKey == 'P') SetMode(EditMode::Spawn, inWindow);
        else if (inKey == 'F') Fit(inWindow);
        else if (inKey == 'S') Save(inWindow);
        else if (inKey == 'A') AddImage(inWindow, false);
        else if (inKey == VK_RETURN) FinishPolygon(inWindow);
        else if (inKey == VK_BACK && !workingPolygon.empty())
        {
            workingPolygon.pop_back();
            InvalidateRect(inWindow, nullptr, FALSE);
        }
        else if (inKey == VK_DELETE && selectedImage.has_value())
        {
            document.images.erase(document.images.begin() + *selectedImage);
            selectedImage.reset();
            InvalidateRect(inWindow, nullptr, FALSE);
        }
        else if (selectedImage.has_value()
            && (inKey == VK_LEFT || inKey == VK_RIGHT || inKey == VK_UP || inKey == VK_DOWN))
        {
            const float amount = GetKeyState(VK_SHIFT) < 0 ? 10.0f : 1.0f;
            MapImage& image = document.images[*selectedImage];
            if (inKey == VK_LEFT) image.x -= amount;
            if (inKey == VK_RIGHT) image.x += amount;
            if (inKey == VK_UP) image.y -= amount;
            if (inKey == VK_DOWN) image.y += amount;
            UpdatePositionControls();
            InvalidateRect(inWindow, nullptr, FALSE);
        }
    }

    void Editor::AddImage(HWND inWindow, const bool inPlaceRight)
    {
        if (document.images.size() >= 2048)
            throw std::runtime_error("A town map supports at most 2048 images.");
        wchar_t fileBuffer[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = inWindow;
        dialog.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;

        const std::filesystem::path source(fileBuffer);
        const std::filesystem::path imageDirectory = assetRoot / L"Images" / L"Towns";
        std::filesystem::create_directories(imageDirectory);
        std::filesystem::path destination = imageDirectory / source.filename();
        int suffix = 1;
        while (std::filesystem::exists(destination))
        {
            std::error_code error;
            if (std::filesystem::equivalent(source, destination, error) && !error) break;
            destination = imageDirectory / (source.stem().wstring() + L"_"
                + std::to_wstring(suffix++) + source.extension().wstring());
        }
        std::error_code equivalentError;
        const bool sameFile = std::filesystem::exists(destination)
            && std::filesystem::equivalent(source, destination, equivalentError) && !equivalentError;
        if (!sameFile)
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);

        Gdiplus::Image imported(destination.c_str());
        if (imported.GetLastStatus() != Gdiplus::Ok || imported.GetWidth() == 0 || imported.GetHeight() == 0)
            throw std::runtime_error("Unable to read the selected image.");

        float x{};
        float y{};
        if (inPlaceRight && !document.images.empty())
        {
            const std::size_t baseIndex = selectedImage.value_or(document.images.size() - 1);
            const MapImage& base = document.images[baseIndex];
            x = base.x + base.width;
            y = base.y;
        }
        else
        {
            x = ReadPosition(positionX);
            y = ReadPosition(positionY);
        }
        const std::filesystem::path relative = std::filesystem::relative(destination, assetRoot);
        const std::string asset = WideToUtf8(relative.generic_wstring());
        if (asset.size() > 240) throw std::runtime_error("Imported asset path exceeds 240 UTF-8 bytes.");
        document.images.push_back(MapImage{ asset, x, y,
            static_cast<float>(imported.GetWidth()), static_cast<float>(imported.GetHeight()) });
        selectedImage = document.images.size() - 1;
        mode = EditMode::Select;
        UpdatePositionControls();
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    float Editor::ReadPosition(HWND inEdit)
    {
        wchar_t text[64]{};
        GetWindowTextW(inEdit, text, static_cast<int>(std::size(text)));
        wchar_t* end{};
        const float value = std::wcstof(text, &end);
        if (end == text || *end != L'\0' || !std::isfinite(value))
            throw std::runtime_error("Position must be a finite number.");
        return value;
    }

    void Editor::ApplyPosition(HWND inWindow)
    {
        if (!selectedImage.has_value()) return;
        MapImage& image = document.images[*selectedImage];
        image.x = ReadPosition(positionX);
        image.y = ReadPosition(positionY);
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    void Editor::UpdatePositionControls() const
    {
        if (!selectedImage.has_value()) return;
        const MapImage& image = document.images[*selectedImage];
        wchar_t text[64]{};
        swprintf_s(text, L"%.2f", image.x);
        SetWindowTextW(positionX, text);
        swprintf_s(text, L"%.2f", image.y);
        SetWindowTextW(positionY, text);
    }

    void Editor::FinishPolygon(HWND inWindow)
    {
        if (workingPolygon.size() < 3) return;
        if (mode == EditMode::Walkable) document.walkablePolygons.push_back(std::move(workingPolygon));
        else if (mode == EditMode::Blocked) document.blockedPolygons.push_back(std::move(workingPolygon));
        workingPolygon.clear();
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    void Editor::SetMode(const EditMode inMode, HWND inWindow)
    {
        workingPolygon.clear();
        mode = inMode;
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    Bounds Editor::CalculateBounds() const
    {
        Bounds bounds{};
        bool initialized = false;
        const auto include = [&bounds, &initialized](const float inX, const float inY)
        {
            if (!initialized)
            {
                bounds = { inX, inY, inX, inY };
                initialized = true;
                return;
            }
            bounds.left = std::min(bounds.left, inX);
            bounds.top = std::min(bounds.top, inY);
            bounds.right = std::max(bounds.right, inX);
            bounds.bottom = std::max(bounds.bottom, inY);
        };
        for (const MapImage& image : document.images)
        {
            include(image.x, image.y);
            include(image.x + image.width, image.y + image.height);
        }
        for (const auto* polygons : { &document.walkablePolygons, &document.blockedPolygons })
            for (const Polygon& polygon : *polygons)
                for (const FloatPoint point : polygon) include(point.x, point.y);
        include(document.spawn.x, document.spawn.y);
        if (!initialized) return document.world;
        if (bounds.right - bounds.left < 1.0f) bounds.right = bounds.left + 1.0f;
        if (bounds.bottom - bounds.top < 1.0f) bounds.bottom = bounds.top + 1.0f;
        return bounds;
    }

    void Editor::Fit(HWND inWindow)
    {
        document.world = CalculateBounds();
        RECT client{};
        GetClientRect(inWindow, &client);
        const float availableWidth = static_cast<float>(std::max(1L, client.right - PANEL_WIDTH));
        const float availableHeight = static_cast<float>(std::max(1L, client.bottom));
        const float width = std::max(1.0f, document.world.right - document.world.left);
        const float height = std::max(1.0f, document.world.bottom - document.world.top);
        zoom = std::clamp(std::min(availableWidth / width, availableHeight / height) * 0.9f,
            MIN_ZOOM, MAX_ZOOM);
        cameraX = document.world.left - (availableWidth / zoom - width) * 0.5f;
        cameraY = document.world.top - (availableHeight / zoom - height) * 0.5f;
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    bool Editor::IsSpawnValid() const
    {
        const auto inAny = [](const FloatPoint inPoint, const std::vector<Polygon>& inPolygons)
        {
            return std::ranges::any_of(inPolygons, [inPoint](const Polygon& inPolygon)
            {
                return IsPointInPolygon(inPoint, inPolygon);
            });
        };
        const auto valid = [&](const FloatPoint inPoint)
        {
            return inAny(inPoint, document.walkablePolygons) && !inAny(inPoint, document.blockedPolygons);
        };
        if (!valid(document.spawn)) return false;
        constexpr float TWO_PI = 6.28318530717958647692f;
        for (int index = 0; index < 16; ++index)
        {
            const float angle = TWO_PI * static_cast<float>(index) / 16.0f;
            if (!valid({ document.spawn.x + std::cos(angle) * 32.0f,
                document.spawn.y + std::sin(angle) * 18.0f })) return false;
        }
        return true;
    }

    void Editor::Paint(HWND inWindow, HDC inDeviceContext)
    {
        RECT client{};
        GetClientRect(inWindow, &client);
        const int canvasRight = std::max(1L, client.right - PANEL_WIDTH);
        HDC buffer = CreateCompatibleDC(inDeviceContext);
        HBITMAP bitmap = CreateCompatibleBitmap(inDeviceContext,
            std::max(1L, client.right), std::max(1L, client.bottom));
        const HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
        FillRect(buffer, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        RECT panel{ canvasRight, 0, client.right, client.bottom };
        HBRUSH panelBrush = CreateSolidBrush(RGB(35, 39, 47));
        FillRect(buffer, &panel, panelBrush);
        DeleteObject(panelBrush);

        Gdiplus::Graphics graphics(buffer);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        ++frameNumber;
        DrawImages(graphics, canvasRight, client.bottom);
        DrawPolygons(graphics, document.walkablePolygons,
            Gdiplus::Color(72, 50, 220, 110), Gdiplus::Color(220, 60, 235, 130));
        DrawPolygons(graphics, document.blockedPolygons,
            Gdiplus::Color(90, 230, 55, 65), Gdiplus::Color(230, 250, 80, 80));
        DrawOverlays(graphics);

        SetBkMode(buffer, TRANSPARENT);
        SetTextColor(buffer, RGB(235, 240, 248));
        const std::wstring instructions = L"Middle drag / Space+drag: pan   Wheel: zoom   Enter: close polygon   Backspace: undo   Delete: image";
        TextOutW(buffer, 12, 10, instructions.c_str(), static_cast<int>(instructions.size()));
        BitBlt(inDeviceContext, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        TrimImageCache();
    }

    void Editor::DrawImages(Gdiplus::Graphics& inGraphics, const int inCanvasWidth,
        const int inCanvasHeight)
    {
        for (const MapImage& image : document.images)
        {
            const Gdiplus::RectF destination = WorldRectangle(image.x, image.y, image.width, image.height);
            if (destination.GetRight() <= 0.0f || destination.GetBottom() <= 0.0f
                || destination.X >= inCanvasWidth || destination.Y >= inCanvasHeight) continue;
            Gdiplus::Image* bitmap = GetCachedImage(image.asset);
            if (bitmap != nullptr)
            {
                inGraphics.DrawImage(bitmap, destination, 0.0f, 0.0f,
                    static_cast<float>(bitmap->GetWidth()), static_cast<float>(bitmap->GetHeight()),
                    Gdiplus::UnitPixel);
            }
            else
            {
                Gdiplus::SolidBrush failed(Gdiplus::Color(255, 90, 20, 30));
                inGraphics.FillRectangle(&failed, destination);
            }
        }
    }

    void Editor::DrawPolygons(Gdiplus::Graphics& inGraphics,
        const std::vector<Polygon>& inPolygons, const Gdiplus::Color inFill,
        const Gdiplus::Color inOutline) const
    {
        Gdiplus::SolidBrush fill(inFill);
        Gdiplus::Pen outline(inOutline, 2.0f);
        for (const Polygon& polygon : inPolygons)
        {
            std::vector<Gdiplus::PointF> points;
            points.reserve(polygon.size());
            for (const FloatPoint point : polygon) points.push_back(WorldPoint(point));
            if (points.size() >= 3)
                inGraphics.FillPolygon(&fill, points.data(), static_cast<INT>(points.size()));
            if (points.size() >= 2)
                inGraphics.DrawPolygon(&outline, points.data(), static_cast<INT>(points.size()));
        }
    }

    void Editor::DrawOverlays(Gdiplus::Graphics& inGraphics) const
    {
        if (!workingPolygon.empty())
        {
            std::vector<Gdiplus::PointF> points;
            for (const FloatPoint point : workingPolygon) points.push_back(WorldPoint(point));
            Gdiplus::Pen pen(Gdiplus::Color(255, 255, 220, 70), 2.0f);
            if (points.size() >= 2)
                inGraphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
            Gdiplus::SolidBrush vertex(Gdiplus::Color(255, 255, 220, 70));
            for (const Gdiplus::PointF point : points)
                inGraphics.FillEllipse(&vertex, point.X - 3.0f, point.Y - 3.0f, 6.0f, 6.0f);
        }

        const Gdiplus::PointF spawn = WorldPoint(document.spawn);
        Gdiplus::SolidBrush spawnBrush(Gdiplus::Color(255, 255, 190, 40));
        inGraphics.FillEllipse(&spawnBrush, spawn.X - 7.0f, spawn.Y - 7.0f, 14.0f, 14.0f);

        if (selectedImage.has_value())
        {
            const MapImage& image = document.images[*selectedImage];
            const Gdiplus::RectF rectangle = WorldRectangle(image.x, image.y, image.width, image.height);
            Gdiplus::Pen pen(Gdiplus::Color(255, 255, 215, 50), 3.0f);
            inGraphics.DrawRectangle(&pen, rectangle);
        }
    }

    Gdiplus::Image* Editor::GetCachedImage(const std::string& inAsset)
    {
        CachedImage& cached = imageCache[inAsset];
        cached.lastUsedFrame = frameNumber;
        if (!cached.image && !cached.loadFailed)
        {
            const std::filesystem::path path = assetRoot / Utf8ToWide(inAsset);
            auto image = std::make_unique<Gdiplus::Image>(path.c_str());
            if (image->GetLastStatus() != Gdiplus::Ok)
            {
                cached.loadFailed = true;
                return nullptr;
            }
            cached.image = std::move(image);
        }
        return cached.image.get();
    }

    void Editor::TrimImageCache()
    {
        while (imageCache.size() > 64)
        {
            auto oldest = imageCache.end();
            std::uint64_t frame = std::numeric_limits<std::uint64_t>::max();
            for (auto iterator = imageCache.begin(); iterator != imageCache.end(); ++iterator)
            {
                if (iterator->second.lastUsedFrame < frame)
                {
                    oldest = iterator;
                    frame = iterator->second.lastUsedFrame;
                }
            }
            if (oldest == imageCache.end() || frame == frameNumber) break;
            imageCache.erase(oldest);
        }
    }

    std::optional<std::size_t> Editor::HitTestImage(const FloatPoint inPoint) const
    {
        for (std::size_t index = document.images.size(); index > 0; --index)
        {
            const MapImage& image = document.images[index - 1];
            if (inPoint.x >= image.x && inPoint.x <= image.x + image.width
                && inPoint.y >= image.y && inPoint.y <= image.y + image.height) return index - 1;
        }
        return std::nullopt;
    }

    FloatPoint Editor::ScreenToWorld(const int inX, const int inY) const
    {
        return { cameraX + static_cast<float>(inX) / zoom,
            cameraY + static_cast<float>(inY) / zoom };
    }

    Gdiplus::PointF Editor::WorldPoint(const FloatPoint inPoint) const
    {
        return { (inPoint.x - cameraX) * zoom, (inPoint.y - cameraY) * zoom };
    }

    Gdiplus::RectF Editor::WorldRectangle(const float inX, const float inY,
        const float inWidth, const float inHeight) const
    {
        return { (inX - cameraX) * zoom, (inY - cameraY) * zoom,
            inWidth * zoom, inHeight * zoom };
    }

    bool Editor::IsCanvasPoint(HWND inWindow, const int inX, const int inY)
    {
        RECT client{};
        GetClientRect(inWindow, &client);
        return inX >= 0 && inY >= 0 && inX < client.right - PANEL_WIDTH && inY < client.bottom;
    }

    void Editor::BeginLeft(HWND inWindow, const int inX, const int inY)
    {
        if (!IsCanvasPoint(inWindow, inX, inY)) return;
        if (GetKeyState(VK_SPACE) < 0)
        {
            BeginPan(inWindow, inX, inY);
            return;
        }
        const FloatPoint world = ScreenToWorld(inX, inY);
        if (mode == EditMode::Select)
        {
            selectedImage = HitTestImage(world);
            if (selectedImage.has_value())
            {
                const MapImage& image = document.images[*selectedImage];
                imageDragOffset = { world.x - image.x, world.y - image.y };
                draggingImage = true;
                SetCapture(inWindow);
                UpdatePositionControls();
            }
        }
        else if (mode == EditMode::Spawn)
        {
            document.spawn = world;
        }
        else
        {
            if (workingPolygon.size() < 2048) workingPolygon.push_back(world);
        }
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    void Editor::EndPointer()
    {
        if (draggingImage || panning)
        {
            draggingImage = false;
            panning = false;
            ReleaseCapture();
        }
    }

    void Editor::BeginPan(HWND inWindow, const int inX, const int inY)
    {
        panning = true;
        lastMouse = POINT{ inX, inY };
        SetCapture(inWindow);
    }

    void Editor::MoveMouse(HWND inWindow, const int inX, const int inY)
    {
        if (panning)
        {
            cameraX -= static_cast<float>(inX - lastMouse.x) / zoom;
            cameraY -= static_cast<float>(inY - lastMouse.y) / zoom;
            lastMouse = POINT{ inX, inY };
            InvalidateRect(inWindow, nullptr, FALSE);
        }
        else if (draggingImage && selectedImage.has_value())
        {
            const FloatPoint world = ScreenToWorld(inX, inY);
            MapImage& image = document.images[*selectedImage];
            image.x = world.x - imageDragOffset.x;
            image.y = world.y - imageDragOffset.y;
            UpdatePositionControls();
            InvalidateRect(inWindow, nullptr, FALSE);
        }
    }

    void Editor::ZoomAt(HWND inWindow, const int inX, const int inY, const int inWheelDelta)
    {
        if (!IsCanvasPoint(inWindow, inX, inY)) return;
        const FloatPoint before = ScreenToWorld(inX, inY);
        const float factor = std::pow(1.15f, static_cast<float>(inWheelDelta) / WHEEL_DELTA);
        zoom = std::clamp(zoom * factor, MIN_ZOOM, MAX_ZOOM);
        cameraX = before.x - static_cast<float>(inX) / zoom;
        cameraY = before.y - static_cast<float>(inY) / zoom;
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    void Editor::PlaceSpawn(HWND inWindow, const int inX, const int inY)
    {
        if (!IsCanvasPoint(inWindow, inX, inY)) return;
        document.spawn = ScreenToWorld(inX, inY);
        InvalidateRect(inWindow, nullptr, FALSE);
    }

    LRESULT CALLBACK WindowProcedure(HWND inWindow, UINT inMessage, WPARAM inWParam, LPARAM inLParam)
    {
        Editor* editor = reinterpret_cast<Editor*>(GetWindowLongPtrW(inWindow, GWLP_USERDATA));
        if (inMessage == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(inLParam);
            editor = static_cast<Editor*>(create->lpCreateParams);
            SetWindowLongPtrW(inWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
        }
        try
        {
            switch (inMessage)
            {
            case WM_CREATE: editor->CreateControls(inWindow); return 0;
            case WM_COMMAND: editor->HandleCommand(inWindow, LOWORD(inWParam)); return 0;
            case WM_KEYDOWN: editor->HandleKey(inWindow, inWParam); return 0;
            case WM_LBUTTONDOWN: editor->BeginLeft(inWindow, GET_X_LPARAM(inLParam), GET_Y_LPARAM(inLParam)); return 0;
            case WM_LBUTTONUP: editor->EndPointer(); return 0;
            case WM_MBUTTONDOWN: editor->BeginPan(inWindow, GET_X_LPARAM(inLParam), GET_Y_LPARAM(inLParam)); return 0;
            case WM_MBUTTONUP: editor->EndPointer(); return 0;
            case WM_RBUTTONDOWN: editor->PlaceSpawn(inWindow, GET_X_LPARAM(inLParam), GET_Y_LPARAM(inLParam)); return 0;
            case WM_MOUSEMOVE: editor->MoveMouse(inWindow, GET_X_LPARAM(inLParam), GET_Y_LPARAM(inLParam)); return 0;
            case WM_MOUSEWHEEL:
            {
                POINT point{ GET_X_LPARAM(inLParam), GET_Y_LPARAM(inLParam) };
                ScreenToClient(inWindow, &point);
                editor->ZoomAt(inWindow, point.x, point.y, GET_WHEEL_DELTA_WPARAM(inWParam));
                return 0;
            }
            case WM_SIZE: editor->LayoutControls(inWindow); InvalidateRect(inWindow, nullptr, FALSE); return 0;
            case WM_PAINT:
            {
                PAINTSTRUCT paint{};
                HDC context = BeginPaint(inWindow, &paint);
                editor->Paint(inWindow, context);
                EndPaint(inWindow, &paint);
                return 0;
            }
            case WM_ERASEBKGND: return 1;
            case WM_DESTROY: PostQuitMessage(0); return 0;
            default: return DefWindowProcW(inWindow, inMessage, inWParam, inLParam);
            }
        }
        catch (const std::exception& inException)
        {
            MessageBoxA(inWindow, inException.what(), "Town Map Editor", MB_OK | MB_ICONERROR);
            return 0;
        }
    }
}

int WINAPI wWinMain(HINSTANCE inInstance, HINSTANCE, PWSTR, int inShowCommand)
{
    ULONG_PTR gdiplusToken{};
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok) return 1;

    int result = 1;
    try
    {
        int argumentCount{};
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        const std::filesystem::path mapPath = argumentCount > 1 ? arguments[1] : L"TownMap.json";
        const std::filesystem::path assetRoot = argumentCount > 2 ? arguments[2] : FindDefaultAssetRoot();
        LocalFree(arguments);

        Editor editor(mapPath, assetRoot);
        const wchar_t* windowClass = L"ActionRPGTownMapEditor";
        WNDCLASSW windowClassInfo{};
        windowClassInfo.lpfnWndProc = WindowProcedure;
        windowClassInfo.hInstance = inInstance;
        windowClassInfo.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClassInfo.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        windowClassInfo.lpszClassName = windowClass;
        RegisterClassW(&windowClassInfo);

        HWND window = CreateWindowExW(0, windowClass, L"Town Map Editor", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr, inInstance, &editor);
        if (window == nullptr) throw std::runtime_error("Unable to create the editor window.");
        ShowWindow(window, inShowCommand);
        editor.HandleCommand(window, FIT_MAP);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        result = static_cast<int>(message.wParam);
    }
    catch (const std::exception& inException)
    {
        MessageBoxA(nullptr, inException.what(), "Town Map Editor", MB_OK | MB_ICONERROR);
    }
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return result;
}
