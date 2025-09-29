#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>  
#include <random>
#include <cmath>
#include <regex>
#include <limits>
#include <map>
#include <SDL2/SDL_ttf.h>

static bool runViewer(const std::string& basePath);
static void editMode(const std::string& basePath);
static void showHelpFromJson(const std::string& jsonPath);
static SDL_Rect drawWindowHeaderWithClose(SDL_Renderer* r, int winW);

struct Point {
    int x, y;
};

using Path = std::vector<Point>;

enum class ShapeType : int {
    Line = 0,
    Circle = 1,
    Triangle = 2,
    Quadrilateral = 3
};

std::vector<Path> readPaths(const std::string& filename) {
    std::ifstream in(filename);
    std::vector<Path> paths;

    if (!in.is_open()) {
        std::cerr << "Cannot open " << filename << "\n";
        return paths;
    }

    int numPaths;
    in >> numPaths;

    for (int i = 0; i < numPaths; i++) {
        int numPoints;
        in >> numPoints;
        Path path;
        for (int j = 0; j < numPoints; j++) {
            int x, y;
            in >> x >> y;
            path.push_back({x, y});
        }
        paths.push_back(path);
    }

    in.close();
    return paths;
}

static bool writePaths(const std::string& filename, const std::vector<Path>& paths) {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Cannot write to " << filename << "\n";
        return false;
    }
    out << paths.size() << "\n";
    for (const auto& path : paths) {
        out << path.size() << "\n";
        for (const auto& p : path) {
            out << p.x << ' ' << p.y << "\n";
        }
    }
    return true;
}

static std::vector<SDL_Color> readColors(const std::string& filename, size_t expectedCount) {
    std::vector<SDL_Color> colors;
    std::ifstream in(filename);
    if (!in.is_open()) {
        colors.resize(expectedCount, SDL_Color{255,255,255,255});
        return colors;
    }
    size_t n = 0; in >> n;
    colors.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        int r=255,g=255,b=255,a=255; in >> r >> g >> b >> a;
        colors.push_back(SDL_Color{ (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a });
    }
    if (colors.size() < expectedCount) colors.resize(expectedCount, SDL_Color{255,255,255,255});
    return colors;
}

static bool writeColors(const std::string& filename, const std::vector<SDL_Color>& colors) {
    std::ofstream out(filename);
    if (!out.is_open()) return false;
    out << colors.size() << "\n";
    for (auto c : colors) {
        out << (int)c.r << ' ' << (int)c.g << ' ' << (int)c.b << ' ' << (int)c.a << "\n";
    }
    return true;
}

static std::vector<ShapeType> readTypes(const std::string& filename, size_t expectedCount) {
    std::vector<ShapeType> types;
    std::ifstream in(filename);
    if (!in.is_open()) {
        types.resize(expectedCount, ShapeType::Line);
        return types;
    }
    size_t n = 0; in >> n;
    types.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        int v = 0; in >> v; if (v < 0 || v > 3) v = 0; types.push_back(static_cast<ShapeType>(v));
    }
    if (types.size() < expectedCount) types.resize(expectedCount, ShapeType::Line);
    return types;
}

static bool writeTypes(const std::string& filename, const std::vector<ShapeType>& types) {
    std::ofstream out(filename);
    if (!out.is_open()) return false;
    out << types.size() << "\n";
    for (auto t : types) { out << static_cast<int>(t) << "\n"; }
    return true;
}

void drawThickPaths(SDL_Renderer* renderer, const std::vector<Path>& paths, SDL_Color color, int thickness) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int r = std::max(0, thickness);
    for (const auto& path : paths) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx*dx + dy*dy > r*r) continue;
                if (path.size() < 2) continue;
                for (size_t i = 1; i < path.size(); i++) {
                    SDL_RenderDrawLine(renderer,
                        path[i-1].x + dx, path[i-1].y + dy,
                        path[i].x + dx, path[i].y + dy);
                }
            }
        }
    }
}

static void drawFilledCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx*dx + dy*dy <= r2) {
                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    }
}

static void drawCircleOutline(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color, int thickness) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int t = -thickness; t <= thickness; ++t) {
        int r = std::max(1, radius + t);
        int r2 = r * r;
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        for (int dy = -r; dy <= r; ++dy) {
            int dx = (int)std::round(std::sqrt(std::max(0, r2 - dy*dy)));
            SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            SDL_RenderDrawPoint(renderer, cx - dx, cy + dy);
        }
    }
}

static bool getBasePathFromCwd(std::string& basePathOut) {
    std::ifstream cwdFile(".cwd");
    if (!cwdFile.is_open()) {
        std::cerr << "Error: Could not open .cwd file in current directory.\n";
        return false;
    }
    std::string line;
    std::getline(cwdFile, line);
    cwdFile.close();
    size_t first = line.find_first_not_of(" \t\n\r");
    size_t last = line.find_last_not_of(" \t\n\r");
    if (first == std::string::npos || last == std::string::npos) {
        std::cerr << "Error: .cwd file is empty or contains only whitespace.\n";
        return false;
    }
    line = line.substr(first, (last - first + 1));
    if (!line.empty() && line[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) {
            std::cerr << "Error: HOME environment variable not set.\n";
            return false;
        }
        basePathOut = std::string(home) + line.substr(1);
    } else {
        basePathOut = line;
    }
    return true;
    }

static void renderStaticSceneColored(SDL_Renderer* renderer,
    const std::vector<Path>& foliage, const std::vector<SDL_Color>& foliageColors, const std::vector<ShapeType>& foliageTypes,
    const std::vector<Path>& trunks, const std::vector<SDL_Color>& trunksColors, const std::vector<ShapeType>& trunksTypes) {
    auto renderOne = [&](const Path& p, SDL_Color c, ShapeType type) {
        if (type == ShapeType::Circle && p.size() >= 2) {
            int cx = (p[0].x + p[1].x) / 2;
            int cy = (p[0].y + p[1].y) / 2;
            int dx = p[0].x - p[1].x; int dy = p[0].y - p[1].y; int radius = (int)std::round(std::sqrt((float)(dx*dx + dy*dy)) / 2.0f);
            drawCircleOutline(renderer, cx, cy, radius, c, 2);
            return;
        }
        if (type == ShapeType::Triangle && p.size() >= 3) {
            Path closed = p; closed.push_back(p.front());
            drawThickPaths(renderer, std::vector<Path>{closed}, c, 2);
            return;
        }
        if (type == ShapeType::Quadrilateral && p.size() >= 4) {
            Path closed = p; closed.push_back(p.front());
            drawThickPaths(renderer, std::vector<Path>{closed}, c, 2);
            return;
        }
       
        drawThickPaths(renderer, std::vector<Path>{p}, c, 2);
    };
    for (size_t i = 0; i < foliage.size(); ++i) {
        SDL_Color c = i < foliageColors.size() ? foliageColors[i] : SDL_Color{255,255,255,255};
        ShapeType t = i < foliageTypes.size() ? foliageTypes[i] : ShapeType::Line;
        renderOne(foliage[i], c, t);
    }
    for (size_t i = 0; i < trunks.size(); ++i) {
        SDL_Color c = i < trunksColors.size() ? trunksColors[i] : SDL_Color{255,255,255,255};
        ShapeType t = i < trunksTypes.size() ? trunksTypes[i] : ShapeType::Line;
        renderOne(trunks[i], c, t);
    }
}

static bool runViewer(const std::string& basePath) {
    std::string pathsFile   = basePath + "/paths.txt";
    std::string currentFile = basePath + "/current.txt";

    auto allPaths    = readPaths(pathsFile);
    auto currentPath = readPaths(currentFile);
    auto allColors   = readColors(pathsFile + ".colors", allPaths.size());
    auto currColors  = readColors(currentFile + ".colors", currentPath.size());
    auto allTypes    = readTypes(pathsFile + ".types", allPaths.size());
    auto currTypes   = readTypes(currentFile + ".types", currentPath.size());
 
    allPaths.clear();
    allColors.clear();

    if (allPaths.empty() && currentPath.empty()) {
        std::cerr << "Warning: No paths were loaded. Check file format and path.\n";
    }

    bool initedVideoHere = false;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }
        initedVideoHere = true;
    }
    bool initedImgHere = false;
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
       
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "SDL_image could not initialize! SDL_image Error: " << IMG_GetError() << "\n";
            if (initedVideoHere) SDL_Quit();
        return false;
        }
        initedImgHere = true;
    }

    SDL_Window* window = SDL_CreateWindow("Atelier Viewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << "\n";
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    bool running = true;
    SDL_Rect closeRect{0,0,0,0};
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                if (mx >= closeRect.x && mx < closeRect.x + closeRect.w && my >= closeRect.y && my < closeRect.y + closeRect.h) {
                    running = false;
                }
            }
        }
        
        SDL_SetRenderDrawColor(renderer, 8, 12, 18, 255);
        SDL_RenderClear(renderer);
      
        closeRect = drawWindowHeaderWithClose(renderer, 800);
        
        renderStaticSceneColored(renderer, allPaths, allColors, allTypes, currentPath, currColors, currTypes);
        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (initedImgHere) IMG_Quit();
    if (initedVideoHere) SDL_Quit();
    return true;
}

static void showHelpFromJson(const std::string& jsonPath) {
    std::ifstream in(jsonPath);
    if (!in.is_open()) {
        std::cerr << "Cannot open commands file: " << jsonPath << "\n";
        return;
    }
    std::string line;
    std::string currentCommand;
    std::regex keyRegex("\\s*\"([^\"]+)\"\\s*:");
    std::regex descRegex("\\s*\"description\"\\s*:\\s*\"([^\"]*)\"");
    while (std::getline(in, line)) {
        std::smatch m;
        if (currentCommand.empty()) {
            if (std::regex_search(line, m, keyRegex)) {
                std::string key = m[1].str();
                if (key != "help" && key != "draw" && key != "edit") {
                   
                }
                currentCommand = key;
            }
        } else {
            if (std::regex_search(line, m, descRegex)) {
                std::string desc = m[1].str();
                std::cout << currentCommand << " - " << desc << "\n";
                currentCommand.clear();
            }
          
            if (line.find('}') != std::string::npos) {
                currentCommand.clear();
            }
        }
    }
}

static std::string collectHelpFromJson(const std::string& jsonPath) {
    std::ifstream in(jsonPath);
    if (!in.is_open()) {
        return std::string("Cannot open commands file: ") + jsonPath + "\n";
    }
    std::string line;
    std::string currentCommand;
    std::regex keyRegex("\\s*\"([^\"]+)\"\\s*:");
    std::regex descRegex("\\s*\"description\"\\s*:\\s*\"([^\"]*)\"");
    std::ostringstream out;
    while (std::getline(in, line)) {
        std::smatch m;
        if (currentCommand.empty()) {
            if (std::regex_search(line, m, keyRegex)) {
                std::string key = m[1].str();
                currentCommand = key;
            }
        } else {
            if (std::regex_search(line, m, descRegex)) {
                std::string desc = m[1].str();
                out << currentCommand << " - " << desc << "\n";
                currentCommand.clear();
            }
            if (line.find('}') != std::string::npos) {
                currentCommand.clear();
            }
        }
    }
    return out.str();
}

static float distanceSquared(int x1, int y1, int x2, int y2) {
    float dx = static_cast<float>(x1 - x2);
    float dy = static_cast<float>(y1 - y2);
    return dx*dx + dy*dy;
}

static float distancePointToSegmentSquared(int px, int py, int x1, int y1, int x2, int y2) {
    float vx = static_cast<float>(x2 - x1);
    float vy = static_cast<float>(y2 - y1);
    float wx = static_cast<float>(px - x1);
    float wy = static_cast<float>(py - y1);
    float c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f) return distanceSquared(px, py, x1, y1);
    float c2 = vx * vx + vy * vy;
    if (c2 <= c1) return distanceSquared(px, py, x2, y2);
    float b = c1 / c2;
    float bx = x1 + b * vx;
    float by = y1 + b * vy;
    return distanceSquared(px, py, static_cast<int>(std::round(bx)), static_cast<int>(std::round(by)));
}

struct Theme {
    SDL_Color background;     
    SDL_Color panel;       
    SDL_Color panelAccent;    
    SDL_Color inputBg;        
    SDL_Color inputBorder;    
    SDL_Color textPrimary;    
    SDL_Color textSecondary;  
    SDL_Color buttonBg;        
    SDL_Color buttonHover;     
    SDL_Color cursor;          
};

static Theme makeDarkOceanTheme() {
    Theme t{};
    t.background    = { 5, 9, 14, 255 };        
    t.panel         = { 14, 22, 35, 210 };     
    t.panelAccent   = { 18, 28, 44, 235 };      
    t.inputBg       = { 9, 14, 22, 230 };
    t.inputBorder   = { 60, 120, 220, 180 };     
    t.textPrimary   = { 236, 242, 252, 255 };   
    t.textSecondary = { 160, 176, 200, 255 };
    t.buttonBg      = { 24, 42, 68, 220 };
    t.buttonHover   = { 34, 62, 98, 240 };
    t.cursor        = { 255, 255, 255, 255 };
    return t;
}

static void setColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static void drawFilledCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color); // fwd

static void drawVerticalGradient(SDL_Renderer* r, SDL_Rect rect, SDL_Color top, SDL_Color bottom) {
    for (int y = 0; y < rect.h; ++y) {
        float t = rect.h <= 1 ? 0.0f : (float)y / (float)(rect.h - 1);
        Uint8 rr = (Uint8)((1.0f - t) * top.r + t * bottom.r);
        Uint8 gg = (Uint8)((1.0f - t) * top.g + t * bottom.g);
        Uint8 bb = (Uint8)((1.0f - t) * top.b + t * bottom.b);
        Uint8 aa = (Uint8)((1.0f - t) * top.a + t * bottom.a);
        SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
        SDL_RenderDrawLine(r, rect.x, rect.y + y, rect.x + rect.w, rect.y + y);
    }
}

static SDL_Rect drawWindowHeaderWithClose(SDL_Renderer* r, int winW) {
    SDL_Rect header{0, 0, winW, 40};
    SDL_Color a{ 18, 28, 44, 220 }, b{ 12, 18, 28, 220 };
    drawVerticalGradient(r, header, a, b);
    
    int radius = 8;
    int cx = winW - 20;
    int cy = header.y + header.h / 2;
    drawFilledCircle(r, cx, cy, radius, { 235, 80, 80, 255 });

    SDL_SetRenderDrawColor(r, 250, 230, 230, 230);
    SDL_RenderDrawLine(r, cx - 3, cy - 3, cx + 3, cy + 3);      
    SDL_RenderDrawLine(r, cx - 3, cy + 3, cx + 3, cy - 3);
   
    SDL_Rect closeRect{ cx - radius - 2, cy - radius - 2, (radius + 2) * 2, (radius + 2) * 2 };
    return closeRect;
}

static void drawWindowHeaderWithControls(SDL_Renderer* r, int winW, SDL_Rect& closeRectOut, SDL_Rect& penRectOut) {
    SDL_Rect header{0, 0, winW, 40};
    SDL_Color a{ 18, 28, 44, 220 }, b{ 12, 18, 28, 220 };
    drawVerticalGradient(r, header, a, b);
    
    int radius = 8;
    int cx = winW - 20;
    int cy = header.y + header.h / 2;
    drawFilledCircle(r, cx, cy, radius, { 235, 80, 80, 255 });
    SDL_SetRenderDrawColor(r, 250, 230, 230, 230);
    SDL_RenderDrawLine(r, cx - 3, cy - 3, cx + 3, cy + 3);
    SDL_RenderDrawLine(r, cx - 3, cy + 3, cx + 3, cy - 3);
    closeRectOut = { cx - radius - 2, cy - radius - 2, (radius + 2) * 2, (radius + 2) * 2 };

    int pcx = winW - 50;
    int pcy = cy;
    drawFilledCircle(r, pcx, pcy, radius, { 90, 150, 255, 230 });

    SDL_SetRenderDrawColor(r, 20, 30, 60, 255);
    SDL_RenderDrawLine(r, pcx - 3, pcy + 2, pcx + 3, pcy - 4);
    SDL_RenderDrawLine(r, pcx - 1, pcy + 2, pcx + 5, pcy - 4);
    penRectOut = { pcx - radius - 2, pcy - radius - 2, (radius + 2) * 2, (radius + 2) * 2 };
}

static void fillRoundedRect(SDL_Renderer* r, SDL_Rect rect, int radius, SDL_Color color) {
    setColor(r, color);

    SDL_Rect body = { rect.x + radius, rect.y, rect.w - 2*radius, rect.h };
    SDL_RenderFillRect(r, &body);

    SDL_Rect left = { rect.x, rect.y + radius, radius, rect.h - 2*radius };
    SDL_Rect right = { rect.x + rect.w - radius, rect.y + radius, radius, rect.h - 2*radius };
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
    
    drawFilledCircle(r, rect.x + radius, rect.y + radius, radius, color);
    drawFilledCircle(r, rect.x + rect.w - radius - 1, rect.y + radius, radius, color);
    drawFilledCircle(r, rect.x + radius, rect.y + rect.h - radius - 1, radius, color);
    drawFilledCircle(r, rect.x + rect.w - radius - 1, rect.y + rect.h - radius - 1, radius, color);
}

static void drawRectBorder(SDL_Renderer* r, SDL_Rect rect, int radius, SDL_Color color) {
    setColor(r, color);
    SDL_RenderDrawLine(r, rect.x + radius, rect.y, rect.x + rect.w - radius, rect.y);
    SDL_RenderDrawLine(r, rect.x + radius, rect.y + rect.h - 1, rect.x + rect.w - radius, rect.y + rect.h - 1);
    SDL_RenderDrawLine(r, rect.x, rect.y + radius, rect.x, rect.y + rect.h - radius);
    SDL_RenderDrawLine(r, rect.x + rect.w - 1, rect.y + radius, rect.x + rect.w - 1, rect.y + rect.h - radius);
}

static TTF_Font* tryLoadAnyFont(int size) {
    const char* candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf"
    };
    for (const char* path : candidates) {
        TTF_Font* f = TTF_OpenFont(path, size);
        if (f) return f;
    }
    return nullptr;
}

static void renderText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color) {
    if (!font) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst{ x, y, s->w, s->h };
    SDL_FreeSurface(s);
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

static void renderWrappedText(SDL_Renderer* r, TTF_Font* font, const std::string& text, SDL_Rect bounds, SDL_Color color) {
    if (!font) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended_Wrapped(font, text.c_str(), color, bounds.w);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst{ bounds.x, bounds.y, s->w, s->h };
    SDL_FreeSurface(s);
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

static bool pointInRect(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

static void drawDropShadow(SDL_Renderer* r, SDL_Rect rect, int radius, int spread, SDL_Color color) {

    for (int i = spread; i >= 1; --i) {
        SDL_Color c = color; c.a = (Uint8)std::max(10, (color.a * i) / (spread * 2));
        SDL_Rect rr{ rect.x - i, rect.y - i, rect.w + 2*i, rect.h + 2*i };
        fillRoundedRect(r, rr, radius + i, c);
    }
}

static void drawUnderline(SDL_Renderer* r, int x, int y, int w, SDL_Color color) {
    setColor(r, color);
    SDL_RenderDrawLine(r, x, y, x + w, y);
}

static bool runGuiTerminal(const std::string& basePath) {
    bool initedVideoHere = false;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << "\n";
            return false;
        }
        initedVideoHere = true;
    }
    if (TTF_Init() == -1) {
        std::cerr << "SDL_ttf could not initialize! TTF_Error: " << TTF_GetError() << "\n";
        if (initedVideoHere) SDL_Quit();
        return false;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Atelier — Terminal",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 700,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << "\n";
        TTF_Quit();
        if (initedVideoHere) SDL_Quit();
        return false;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        TTF_Quit();
        if (initedVideoHere) SDL_Quit();
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    TTF_Font* font14 = tryLoadAnyFont(14);
    TTF_Font* font16 = tryLoadAnyFont(16);
    TTF_Font* font20 = tryLoadAnyFont(20);
    if (!font14 || !font16 || !font20) {
        std::cerr << "Failed to load a font. Falling back to CLI.\n";
        if (font14) TTF_CloseFont(font14);
        if (font16) TTF_CloseFont(font16);
        if (font20) TTF_CloseFont(font20);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    Theme theme = makeDarkOceanTheme();

    SDL_Rect header = { 0, 0, 1000, 72 };
    SDL_Rect outputPanel = { 40, 100, 920, 460 };
    SDL_Rect inputPanel = { 40, 580, 920, 62 };

    std::vector<std::string> outputLines;
    auto appendOutput = [&](const std::string& s) {
        std::istringstream ss(s);
        std::string line;
        while (std::getline(ss, line)) {
            outputLines.push_back(line);
        }     
        if (outputLines.size() > 1000) {
            outputLines.erase(outputLines.begin(), outputLines.begin() + (outputLines.size() - 1000));
        }
    };

    appendOutput("Atelier Terminal. Type help for commands.");

    std::string inputText;
    size_t historyIndex = 0;
    std::vector<std::string> history;
    bool running = true;

    SDL_StartTextInput();

    auto executeCommand = [&](const std::string& cmdRaw) {
        std::string cmd = cmdRaw;
        if (cmd == "") return;
        appendOutput(std::string("> ") + cmd);
        if (cmd == "help") {
            appendOutput(collectHelpFromJson("/home/user/Atelier_lab/Programs/commands.json"));
        } else if (cmd == "clear") {
            outputLines.clear();
        } else if (cmd == "draw") {
            SDL_StopTextInput();
            runViewer(basePath);
            SDL_StartTextInput();
        } else if (cmd == "edit") {
            SDL_StopTextInput();
            editMode(basePath);
            SDL_StartTextInput();
        } else if (cmd == "exit" || cmd == "quit") {
            running = false;
        } else {
            appendOutput("Unknown command. Type 'help'.");
        }
    };

    while (running) {
        // events
        SDL_Event e;
        int mx = -1, my = -1;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_TEXTINPUT) {
                inputText += std::string(e.text.text);
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_BACKSPACE) {
                    if (!inputText.empty()) inputText.pop_back();
                } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    history.push_back(inputText);
                    historyIndex = history.size();
                    executeCommand(inputText);
                    inputText.clear();
                } else if (e.key.keysym.sym == SDLK_UP) {
                    if (historyIndex > 0) {
                        historyIndex--;
                        inputText = history[historyIndex];
                    }
                } else if (e.key.keysym.sym == SDLK_DOWN) {
                    if (historyIndex + 1 < history.size()) {
                        historyIndex++;
                        inputText = history[historyIndex];
                    } else {
                        historyIndex = history.size();
                        inputText.clear();
                    }
                } else if (e.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
            }
        }

        // draw background gradient for more modern depth
        drawVerticalGradient(renderer, {0, 0, 1000, 700}, { 6, 10, 16, 255 }, { 10, 16, 26, 255 });

        // header bar (glass, minimal)
        fillRoundedRect(renderer, header, 0, theme.panelAccent);
        // App mark (small circle) and centered title
        drawFilledCircle(renderer, 28, header.y + 36, 6, { 90, 160, 255, 200 });
        drawFilledCircle(renderer, 46, header.y + 36, 6, { 90, 255, 210, 200 });
        const char* title = "Atelier Terminal";
        int tw = 0, th = 0; TTF_SizeUTF8(font20, title, &tw, &th);
        renderText(renderer, font20, title, header.x + (header.w - tw) / 2, header.y + (header.h - th) / 2, theme.textPrimary);
        drawUnderline(renderer, 40, header.h - 1, 920, { 60, 120, 220, 80 });

        // output panel
        drawDropShadow(renderer, outputPanel, 16, 8, { 0, 0, 0, 55 });
        fillRoundedRect(renderer, outputPanel, 16, theme.panel);
        // clip to output panel when drawing text
        SDL_Rect clip = outputPanel;
        SDL_RenderSetClipRect(renderer, &clip);
        int lineHeight = TTF_FontHeight(font14) + 2;
        int maxVisible = (outputPanel.h - 24) / lineHeight;
        int startIdx = (int)outputLines.size() - maxVisible;
        if (startIdx < 0) startIdx = 0;
        int y = outputPanel.y + 12;
        for (size_t i = (size_t)startIdx; i < outputLines.size(); ++i) {
            renderText(renderer, font14, outputLines[i], outputPanel.x + 16, y, theme.textPrimary);
            y += lineHeight;
        }
        SDL_RenderSetClipRect(renderer, nullptr);

        // input panel
        drawDropShadow(renderer, inputPanel, 14, 8, { 0, 0, 0, 65 });
        fillRoundedRect(renderer, inputPanel, 14, theme.inputBg);
        drawRectBorder(renderer, { inputPanel.x, inputPanel.y, inputPanel.w, inputPanel.h }, 14, { 80, 150, 255, 90 });
        drawRectBorder(renderer, { inputPanel.x - 1, inputPanel.y - 1, inputPanel.w + 2, inputPanel.h + 2 }, 16, theme.inputBorder);
        renderText(renderer, font16, inputText.empty() ? std::string("Type a command…") : inputText,
                   inputPanel.x + 16, inputPanel.y + 16, inputText.empty() ? theme.textSecondary : theme.textPrimary);
        // caret
        int textW = 0, textH = 0; TTF_SizeUTF8(font16, inputText.c_str(), &textW, &textH);
        int caretX = inputPanel.x + 16 + textW, caretW = 2;
        int caretH = TTF_FontHeight(font16);
        bool blink = ((SDL_GetTicks() / 500) % 2) == 0;
        if (blink) { SDL_Rect caret{ caretX, inputPanel.y + 14, caretW, caretH }; setColor(renderer, theme.cursor); SDL_RenderFillRect(renderer, &caret); }

        SDL_RenderPresent(renderer);
    }

    SDL_StopTextInput();
    TTF_CloseFont(font14);
    TTF_CloseFont(font16);
    TTF_CloseFont(font20);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    if (initedVideoHere) SDL_Quit();
    return true;
}

static bool exitCliAfterSDL = false;

static void editMode(const std::string& basePath) {
    std::string pathsFile   = basePath + "/paths.txt";
    std::string currentFile = basePath + "/current.txt";
    auto foliage = readPaths(pathsFile);
    auto trunks  = readPaths(currentFile);
    // Load per-path colors (defaults to white if file missing/short)
    std::vector<SDL_Color> foliageColors = readColors(pathsFile + ".colors", foliage.size());
    std::vector<SDL_Color> trunksColors  = readColors(currentFile + ".colors", trunks.size());
    // Load per-path types
    std::vector<ShapeType> foliageTypes = readTypes(pathsFile + ".types", foliage.size());
    std::vector<ShapeType> trunksTypes  = readTypes(currentFile + ".types", trunks.size());
    SDL_Color currentDrawColor{255,255,255,255};
    bool showColorPanel = false;
    ShapeType currentShape = ShapeType::Line;
    // Placement state: collect exact number of clicks for chosen shape
    int placementNeededPoints = 0;  // 0 when idle
    int placementCollected = 0;
    Path placementPoints;

    bool initedVideoHere = false;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << "\n";
        return;
    }
        initedVideoHere = true;
    }
    bool initedImgHere = false;
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
        std::cerr << "SDL_image could not initialize! SDL_image Error: " << IMG_GetError() << "\n";
            if (initedVideoHere) SDL_Quit();
        return;
        }
        initedImgHere = true;
    }
    SDL_Window* window = SDL_CreateWindow("Atelier Editor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << "\n";
        IMG_Quit();
        SDL_Quit();
        return;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return;
    }

    // Interaction state
    bool running = true;
    bool awaitingSecondPoint = false; // legacy; no longer used for placement
    Point firstPoint {0, 0};
    bool dragging = false;
    bool draggingIsFoliage = true;
    size_t draggingPathIndex = 0;
    int draggingPointIndex = 0; // 0 or last for two-point paths
    const float selectRadius2 = 12.0f * 12.0f;

    auto redraw = [&]() {
        SDL_SetRenderDrawColor(renderer, 8, 12, 18, 255);
        SDL_RenderClear(renderer);
        // header with close + pen
        SDL_Rect closeRect, penRect; drawWindowHeaderWithControls(renderer, 800, closeRect, penRect);
        // content
        renderStaticSceneColored(renderer, foliage, foliageColors, foliageTypes, trunks, trunksColors, trunksTypes);
        // color panel (if shown)
        if (showColorPanel) {
            SDL_Rect panel{800 - 220, 50, 200, 500};
            drawDropShadow(renderer, panel, 12, 6, {0,0,0,60});
            fillRoundedRect(renderer, panel, 12, {14, 22, 35, 230});
            // small close button in panel header
            SDL_Rect panelClose{ panel.x + panel.w - 24, panel.y + 8, 12, 12 };
            drawFilledCircle(renderer, panelClose.x + 6, panelClose.y + 6, 6, {235,80,80,255});
            // simple color wheel: rows of hues + alpha slider approximation
            int cx = panel.x + panel.w/2; int cy = panel.y + 130; int rad = 80;
            // wheel by sampling angle and radius bands
            for (int rr = rad; rr >= rad-20; --rr) {
                for (int a = 0; a < 360; ++a) {
                    float t = a / 60.0f; int sector = (int)std::floor(t); float f = t - sector; float p = 0, q = 1 - f, s = f;
                    float r=1,g=0,b=0;
                    switch (sector % 6) {
                        case 0: r=1; g=s; b=0; break;
                        case 1: r=q; g=1; b=0; break;
                        case 2: r=0; g=1; b=s; break;
                        case 3: r=0; g=q; b=1; break;
                        case 4: r=s; g=0; b=1; break;
                        case 5: r=1; g=0; b=q; break;
                    }
                    int x = cx + (int)std::round(std::cos(a * 3.1415926f / 180.0f) * rr);
                    int y = cy + (int)std::round(std::sin(a * 3.1415926f / 180.0f) * rr);
                    SDL_SetRenderDrawColor(renderer, (Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255), 255);
                    SDL_RenderDrawPoint(renderer, x, y);
                }
            }
            // current color swatch
            SDL_Rect sw{ panel.x + 20, panel.y + 260, panel.w - 40, 30 };
            fillRoundedRect(renderer, sw, 6, currentDrawColor);
            drawRectBorder(renderer, sw, 6, {255,255,255,100});
            // shape selectors
            TTF_Font* panelFont = tryLoadAnyFont(14);
            auto drawShapeBtn = [&](const char* label, int y, bool active) {
                SDL_Rect r{ panel.x + 20, y, panel.w - 40, 28 };
                fillRoundedRect(renderer, r, 6, active ? SDL_Color{34, 62, 98, 240} : SDL_Color{24, 42, 68, 220});
                renderText(renderer, panelFont, label, r.x + 10, r.y + 6, {236,242,252,255});
                return r;
            };
            SDL_Rect btnLine = drawShapeBtn("Line", panel.y + 310, currentShape == ShapeType::Line);
            SDL_Rect btnCircle = drawShapeBtn("Circle", panel.y + 344, currentShape == ShapeType::Circle);
            SDL_Rect btnTriangle = drawShapeBtn("Triangle", panel.y + 378, currentShape == ShapeType::Triangle);
            SDL_Rect btnQuad = drawShapeBtn("Quadrilateral", panel.y + 412, currentShape == ShapeType::Quadrilateral);
            if (panelFont) TTF_CloseFont(panelFont);

            // store for hit testing via static variables captured by redraw scope
            // we can't store state from redraw alone; handled in event section
        }
        // Indicate placement in progress: show last placed point
        if (placementCollected > 0 && !placementPoints.empty()) {
            Point lp = placementPoints.back();
            drawFilledCircle(renderer, lp.x, lp.y, 4, {255, 255, 255, 200});
        }
        SDL_RenderPresent(renderer);
    };

    redraw();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    // Cancel current interaction
                    awaitingSecondPoint = false;
                    dragging = false;
                    redraw();
                } else if (shiftHeld && (e.key.keysym.sym == SDLK_d)) {
                    exitCliAfterSDL = true;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x;
                int my = e.button.y;
                // header controls hit test (top-right)
                SDL_Rect closeRect, penRect; drawWindowHeaderWithControls(renderer, 800, closeRect, penRect);
                if (mx >= closeRect.x && mx < closeRect.x + closeRect.w && my >= closeRect.y && my < closeRect.y + closeRect.h) {
                    running = false;
                    break;
                }
                if (mx >= penRect.x && mx < penRect.x + penRect.w && my >= penRect.y && my < penRect.y + penRect.h) {
                    showColorPanel = !showColorPanel;
                    redraw();
                    continue;
                }
                if (showColorPanel) {
                    SDL_Rect panel{800 - 220, 50, 200, 500};
                    SDL_Rect panelClose{ panel.x + panel.w - 24, panel.y + 8, 12, 12 };
                    int pcx = panelClose.x + 6, pcy = panelClose.y + 6;
                    // close color panel
                    int dx = mx - pcx, dy = my - pcy; if (dx*dx + dy*dy <= 6*6) { showColorPanel = false; redraw(); continue; }
                    // pick color from wheel edge bands
                    int cx = panel.x + panel.w/2; int cy = panel.y + 130; int rad = 80;
                    int dxw = mx - cx, dyw = my - cy; float dist = std::sqrt((float)(dxw*dxw + dyw*dyw));
                    if (dist <= rad && dist >= rad-20) {
                        float ang = std::atan2((float)dyw, (float)dxw); if (ang < 0) ang += 2.0f * 3.1415926f;
                        float deg = ang * 180.0f / 3.1415926f; float t = deg / 60.0f; int sector = (int)std::floor(t); float f = t - sector; float q = 1 - f, s = f;
                        float rr=1, gg=0, bb=0;
                        switch (sector % 6) {
                            case 0: rr=1; gg=s; bb=0; break;
                            case 1: rr=q; gg=1; bb=0; break;
                            case 2: rr=0; gg=1; bb=s; break;
                            case 3: rr=0; gg=q; bb=1; break;
                            case 4: rr=s; gg=0; bb=1; break;
                            case 5: rr=1; gg=0; bb=q; break;
                        }
                        currentDrawColor = SDL_Color{ (Uint8)(rr*255), (Uint8)(gg*255), (Uint8)(bb*255), 255 };
                        redraw();
                        continue;
                    }
                    // shape selection hit test
                    auto inside = [&](SDL_Rect r){ return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h; };
                    SDL_Rect btnLine{ panel.x + 20, panel.y + 310, panel.w - 40, 28 };
                    SDL_Rect btnCircle{ panel.x + 20, panel.y + 344, panel.w - 40, 28 };
                    SDL_Rect btnTriangle{ panel.x + 20, panel.y + 378, panel.w - 40, 28 };
                    SDL_Rect btnQuad{ panel.x + 20, panel.y + 412, panel.w - 40, 28 };
                    if (inside(btnLine)) { currentShape = ShapeType::Line; placementPoints.clear(); placementCollected = 0; placementNeededPoints = 0; redraw(); continue; }
                    if (inside(btnCircle)) { currentShape = ShapeType::Circle; placementPoints.clear(); placementCollected = 0; placementNeededPoints = 0; redraw(); continue; }
                    if (inside(btnTriangle)) { currentShape = ShapeType::Triangle; placementPoints.clear(); placementCollected = 0; placementNeededPoints = 0; redraw(); continue; }
                    if (inside(btnQuad)) { currentShape = ShapeType::Quadrilateral; placementPoints.clear(); placementCollected = 0; placementNeededPoints = 0; redraw(); continue; }
                }
                bool shiftHeld = (SDL_GetModState() & KMOD_SHIFT) != 0;
                bool ctrlHeld  = (SDL_GetModState() & KMOD_CTRL)  != 0;

                // Ctrl+Shift+Click: delete nearest shape (line/circle/polygon)
                if (ctrlHeld && shiftHeld) {
                    const float tolerance2 = 14.0f * 14.0f; // clickable radius
                    float bestMetric = 1e12f;
                    bool bestIsFoliage = true;
                    size_t bestIndex = (size_t)-1;
                    auto scoreLine = [&](const Path& p){ return distancePointToSegmentSquared(mx, my, p[0].x, p[0].y, p[1].x, p[1].y); };
                    auto scoreCircle = [&](const Path& p){
                        int cx = (p[0].x + p[1].x)/2; int cy = (p[0].y + p[1].y)/2;
                        float R = std::sqrt((float)((p[0].x-p[1].x)*(p[0].x-p[1].x) + (p[0].y-p[1].y)*(p[0].y-p[1].y))) / 2.0f;
                        float d = std::sqrt((float)((mx-cx)*(mx-cx) + (my-cy)*(my-cy)));
                        float diff = std::fabs(d - R);
                        return diff*diff;
                    };
                    auto scorePoly = [&](const Path& p){
                        float best = 1e12f;
                        for (size_t i = 1; i < p.size(); ++i) {
                            best = std::min(best, distancePointToSegmentSquared(mx, my, p[i-1].x, p[i-1].y, p[i].x, p[i].y));
                        }
                        best = std::min(best, distancePointToSegmentSquared(mx, my, p.back().x, p.back().y, p.front().x, p.front().y));
                        return best;
                    };
                    auto consider = [&](const std::vector<Path>& vec, const std::vector<ShapeType>& types, bool isFoliage){
                        for (size_t i = 0; i < vec.size(); ++i) {
                            float s = 1e12f;
                            ShapeType t = (i < types.size() ? types[i] : ShapeType::Line);
                            const Path& p = vec[i];
                            if (t == ShapeType::Line && p.size() >= 2) s = scoreLine(p);
                            else if (t == ShapeType::Circle && p.size() >= 2) s = scoreCircle(p);
                            else if (t == ShapeType::Triangle && p.size() >= 3) s = scorePoly(p);
                            else if (t == ShapeType::Quadrilateral && p.size() >= 4) s = scorePoly(p);
                            else if (p.size() >= 2) s = scoreLine(p);
                            if (s < bestMetric) { bestMetric = s; bestIsFoliage = isFoliage; bestIndex = i; }
                        }
                    };
                    consider(foliage, foliageTypes, true);
                    consider(trunks, trunksTypes, false);
                    if (bestIndex != (size_t)-1 && bestMetric <= tolerance2) {
                        if (bestIsFoliage) {
                            foliage.erase(foliage.begin() + bestIndex);
                            if (bestIndex < foliageColors.size()) foliageColors.erase(foliageColors.begin() + bestIndex);
                            if (bestIndex < foliageTypes.size()) foliageTypes.erase(foliageTypes.begin() + bestIndex);
                        } else {
                            trunks.erase(trunks.begin() + bestIndex);
                            if (bestIndex < trunksColors.size()) trunksColors.erase(trunksColors.begin() + bestIndex);
                            if (bestIndex < trunksTypes.size()) trunksTypes.erase(trunksTypes.begin() + bestIndex);
                        }
                        redraw();
                        continue;
                    }
                    // Ctrl+Shift was pressed but nothing matched; do not create anything on this click
                    continue;
                }

                if (shiftHeld) {
                    // initialize placement state if idle
                    if (placementNeededPoints == 0) {
                        placementPoints.clear();
                        placementCollected = 0;
                        switch (currentShape) {
                            case ShapeType::Line: placementNeededPoints = 2; break;
                            case ShapeType::Circle: placementNeededPoints = 2; break;
                            case ShapeType::Triangle: placementNeededPoints = 3; break;
                            case ShapeType::Quadrilateral: placementNeededPoints = 4; break;
                        }
                    }
                    placementPoints.push_back({mx, my});
                    placementCollected++;
                    if (placementCollected >= placementNeededPoints) {
                        // finalize shape
                        Path p = placementPoints;
                        trunks.push_back(p);
                        trunksColors.push_back(currentDrawColor);
                        trunksTypes.push_back(currentShape);
                        // reset placement
                        placementPoints.clear();
                        placementCollected = 0;
                        placementNeededPoints = 0;
                    }
                    redraw();
                } else if (ctrlHeld) {
                    // Try to pick nearest endpoint among two-point paths
                    float bestDist2 = 1e9f;
                    bool found = false;
                    // foliage
                    for (size_t i = 0; i < foliage.size(); ++i) {
                        const auto& path = foliage[i];
                        if (path.size() == 2) {
                            float d0 = distanceSquared(mx, my, path[0].x, path[0].y);
                            float d1 = distanceSquared(mx, my, path[1].x, path[1].y);
                            if (d0 < bestDist2 && d0 <= selectRadius2) { bestDist2 = d0; dragging = true; draggingIsFoliage = true; draggingPathIndex = i; draggingPointIndex = 0; found = true; }
                            if (d1 < bestDist2 && d1 <= selectRadius2) { bestDist2 = d1; dragging = true; draggingIsFoliage = true; draggingPathIndex = i; draggingPointIndex = 1; found = true; }
                        }
                    }
                    // trunks
                    for (size_t i = 0; i < trunks.size(); ++i) {
                        const auto& path = trunks[i];
                        if (path.size() == 2) {
                            float d0 = distanceSquared(mx, my, path[0].x, path[0].y);
                            float d1 = distanceSquared(mx, my, path[1].x, path[1].y);
                            if (d0 < bestDist2 && d0 <= selectRadius2) { bestDist2 = d0; dragging = true; draggingIsFoliage = false; draggingPathIndex = i; draggingPointIndex = 0; found = true; }
                            if (d1 < bestDist2 && d1 <= selectRadius2) { bestDist2 = d1; dragging = true; draggingIsFoliage = false; draggingPathIndex = i; draggingPointIndex = 1; found = true; }
                        }
                    }
                    if (!found) dragging = false;
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                if (dragging) {
                    int mx = e.motion.x;
                    int my = e.motion.y;
                    if (draggingIsFoliage) {
                        if (draggingPointIndex == 0) foliage[draggingPathIndex][0] = {mx, my};
                        else foliage[draggingPathIndex][1] = {mx, my};
                    } else {
                        // move nearest endpoint on the selected path
                        if (trunksTypes.size() > draggingPathIndex) {
                            ShapeType t = trunksTypes[draggingPathIndex];
                            if (t == ShapeType::Triangle && trunks[draggingPathIndex].size() >= 3) {
                                // move whichever vertex is closest among 3
                                int idx = 0; float best = 1e9f;
                                for (int k = 0; k < 3; ++k) {
                                    float d = distanceSquared(mx, my, trunks[draggingPathIndex][k].x, trunks[draggingPathIndex][k].y);
                                    if (d < best) { best = d; idx = k; }
                                }
                                trunks[draggingPathIndex][idx] = {mx, my};
                            } else if (t == ShapeType::Quadrilateral && trunks[draggingPathIndex].size() >= 4) {
                                int idx = 0; float best = 1e9f;
                                for (int k = 0; k < 4; ++k) {
                                    float d = distanceSquared(mx, my, trunks[draggingPathIndex][k].x, trunks[draggingPathIndex][k].y);
                                    if (d < best) { best = d; idx = k; }
                                }
                                trunks[draggingPathIndex][idx] = {mx, my};
                            } else {
                                if (draggingPointIndex == 0) trunks[draggingPathIndex][0] = {mx, my};
                                else trunks[draggingPathIndex][1] = {mx, my};
                            }
                    } else {
                        if (draggingPointIndex == 0) trunks[draggingPathIndex][0] = {mx, my};
                        else trunks[draggingPathIndex][1] = {mx, my};
                        }
                    }
                    redraw();
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            }
        }
        SDL_Delay(8);
    }

    // Save edits on close
    writePaths(pathsFile, foliage);
    writePaths(currentFile, trunks);
    writeColors(pathsFile + ".colors", foliageColors);
    writeColors(currentFile + ".colors", trunksColors);
    writeTypes(pathsFile + ".types", foliageTypes);
    writeTypes(currentFile + ".types", trunksTypes);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (initedImgHere) IMG_Quit();
    if (initedVideoHere) SDL_Quit();
}

int main() {
    std::string basePath;
    if (!getBasePathFromCwd(basePath)) {
        return 1;
    }

    // Prefer GUI; if it fails (e.g., no fonts), fall back to CLI
    if (!runGuiTerminal(basePath)) {
        std::cout << "Atelier Terminal — type 'help' for commands. Type 'exit' to quit.\n";
    while (true) {
        std::cout << "> ";
        std::string cmd;
        if (!(std::cin >> cmd)) break;
        if (cmd == "help") {
            showHelpFromJson("/home/user/Atelier_lab/Programs/commands.json");
        } else if (cmd == "draw") {
            runViewer(basePath);
            if (exitCliAfterSDL) { break; }
        } else if (cmd == "edit") {
            editMode(basePath);
            if (exitCliAfterSDL) { break; }
        } else if (cmd == "exit" || cmd == "quit") {
            break;
        } else {
            std::cout << "Unknown command. Type 'help'.\n";
            }
        }
    }
    return 0;
}
