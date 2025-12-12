#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <algorithm>

// Ejemplo 2D tipo "Minecraft" usando SFML con físicas básicas solo para el jugador
// Características añadidas:
// - Física vertical: gravedad, salto, velocidad y colisión con tiles sólidos
// - Mapa más grande y una cueva/túnel subterráneo

// Map size increased: larger world while window/view remains the same
const int W = 240;
const int H = 120;
const int TILE = 32;

enum Block : char { AIR = ' ', GRASS = 'G', DIRT = 'D', STONE = 'S', WOOD = 'W', BEDR = 'B', LEAF = 'L', COAL = 'c', IRON = 'i', GOLD = 'o' };
// New biomes blocks
enum ExtraBlock : char { SAND = 'N', SNOW = 'Y', NETH = 'H', LAVA = 'V' };

using World = std::vector<std::string>;

struct Player {
    float px, py; // posición en píxeles
    float vx, vy; // velocidad en píxeles/s
    int fx, fy;   // dirección de mirada (-1/0/1 en x, y)
    char selected;
    std::map<char,int> inv;
    std::map<std::string,int> tools; // herramientas: "pickaxe","axe","shovel"
    std::string selectedTool; // key of selected tool
    float w, h; // tamaño del rectángulo del jugador
};

bool in_bounds(int x,int y){ return x>=0 && x<W && y>=0 && y<H; }
bool isSolid(char b){ return b!=(char)AIR; }

char get_block(const World &w, int x,int y){ if(!in_bounds(x,y)) return (char)BEDR; return w[y][x]; }
void set_block(World &w,int x,int y,char b){ if(in_bounds(x,y)) w[y][x]=b; }

void init_world(World &world) {
    // Procedural: generar altura de superficie por columna y cavidades/túneles
    world.assign(H, std::string(W, (char)AIR));
    std::srand((unsigned)time(nullptr));
    std::vector<int> height(W);
    for (int x = 0; x < W; ++x) {
        float t = (float)x / (float)W * 6.2831853f; // 2*pi
        float base = (std::sin(t * 0.7f) + 1.0f) * 0.5f; // 0..1
        int h = (int)((H / 3) + base * (H / 6)) + (std::rand() % 3 - 1);
        h = std::max(2, std::min(H-6, h));
        height[x] = h;
    }

    // rellenar suelo según heights, aplicando biomas: left third = desert, middle = normal, right = snow
    for (int x = 0; x < W; ++x) {
        int g = height[x];
        int region = (x * 3) / W; // 0,1,2
        for (int y = g; y < H-1; ++y) {
            if (y == g) {
                if (region == 0) world[y][x] = (char)SAND; // desert
                else if (region == 2) world[y][x] = (char)SNOW; // snow
                else world[y][x] = (char)GRASS;
            }
            else if (y < g + 4) {
                if (region == 0) world[y][x] = (char)SAND;
                else world[y][x] = (char)DIRT;
            }
            else world[y][x] = (char)STONE;
        }
    }
    // bedrock
    for (int x = 0; x < W; ++x) world[H-1][x] = (char)BEDR;

    // Infierno (nether) en la parte inferior: capas de NETH con bolsas de LAVA encima de la roca profunda
    int nethDepth = std::max(6, H/12); // number of rows above bedrock for the 'infierno' (larger)
    for (int y = H-1 - nethDepth; y < H-1; ++y) {
        for (int x = 0; x < W; ++x) {
            // no sobreescribir bedrock
            if (y >= 0 && y < H-1) {
                // mezclar lava en parches (más lava, más profundo)
                if ((std::rand() % 100) < 40 && y >= H-2) world[y][x] = (char)LAVA;
                else world[y][x] = (char)NETH;
            }
        }
    }

    // árboles: probabilidad por columna, tronco vertical y copa de hojas (no en desierto, más en snow)
    for (int x = 2; x < W-2; ++x) {
        int region = (x * 3) / W;
        int treeChance = (region == 0) ? 3 : (region == 2 ? 18 : 12); // desert few, snow more
        if ((std::rand() % 100) < treeChance) {
            int g = height[x];
            // avoid trees if desert (surface is sand)
            if (region == 0) continue;
            int trunkH = 2 + (std::rand() % 3); // 2..4
            for (int t = 1; t <= trunkH; ++t) {
                int ty = g - t;
                if (ty >= 0) world[ty][x] = (char)WOOD;
            }
            int topY = g - trunkH;
            // copa: block of ~5x3
            for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 0; ++dy) {
                int xx = x + dx; int yy = topY + dy;
                if (in_bounds(xx, yy) && world[yy][xx] == (char)AIR) {
                    if (region == 2) world[yy][xx] = (char)SNOW; else world[yy][xx] = (char)LEAF;
                }
            }
        }
    }

    // Crear cuevas/túneles: más largos y profundos, con mayor probabilidad y variación
    int tunnels = 6 + (std::rand() % 6);
    for (int i = 0; i < tunnels; ++i) {
        int tx = std::max(2, std::min(W-3, (std::rand() % W)));
        // comenzar más profundo para no afectar la capa de superficie
        int ty = std::min(H-6, height[tx] + 8 + (std::rand() % 6));
        int len = 40 + (std::rand() % 120); // túneles más largos
        for (int s = 0; s < len; ++s) {
            // radio variable (0..2) para cuevas más anchas en partes
            int radius = (std::rand() % 3);
            for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
                int xx = tx + dx; int yy = ty + dy;
                // no cavar en la capa superior cercana (proteger altura de columna)
                if (in_bounds(xx, yy) && yy < H-2 && yy > height[tx] + 2) world[yy][xx] = (char)AIR;
            }
            // random walk con mayor variación vertical y sesgo horizontal
            tx += (std::rand() % 5) - 2;
            ty += (std::rand() % 5) - 2;
            if (tx < 1) tx = 1; if (tx > W-2) tx = W-2;
            if (ty < 2) ty = 2; if (ty > H-3) ty = H-3;
        }
    }

    // Generar vetas de mineral: reemplazar algo de piedra por carbón/hierro/oro según profundidad
    for (int y = 2; y < H-2; ++y) {
        for (int x = 1; x < W-1; ++x) {
            if (world[y][x] == (char)STONE) {
                int depth = y;
                int r = std::rand() % 1000;
                // carbón: más frecuente en capas superiores de roca
                if (r < 40 && depth < H/2) world[y][x] = (char)COAL; // ~4%
                // hierro: menos frecuente y más profundo
                else if (r < 52 && depth >= H/4 && depth < (3*H)/4) world[y][x] = (char)IRON; // ~1.2%
                // oro: raro, profundo
                else if (r < 55 && depth > (3*H)/4) world[y][x] = (char)GOLD; // ~0.3%
            }
        }
    }
}

// Helpers para detección de colisiones AABB -> tiles
void resolveHorizontal(World &world, Player &p, float newPx) {
    float left = newPx;
    float right = newPx + p.w - 1;
    int topTile = std::floor(p.py / TILE);
    int bottomTile = std::floor((p.py + p.h - 1) / TILE);
    int leftTile = std::floor(left / TILE);
    int rightTile = std::floor(right / TILE);
    if (p.vx > 0) {
        for (int tx = rightTile; tx <= rightTile; ++tx) {
            for (int ty = topTile; ty <= bottomTile; ++ty) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    p.px = tx * TILE - p.w; p.vx = 0; return;
                }
            }
        }
    } else if (p.vx < 0) {
        for (int tx = leftTile; tx >= leftTile; --tx) {
            for (int ty = topTile; ty <= bottomTile; ++ty) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    p.px = (tx+1) * TILE; p.vx = 0; return;
                }
            }
        }
    }
    p.px = newPx;
}

void resolveVertical(World &world, Player &p, float newPy) {
    float top = newPy;
    float bottom = newPy + p.h - 1;
    int leftTile = std::floor(p.px / TILE);
    int rightTile = std::floor((p.px + p.w - 1) / TILE);
    int topTile = std::floor(top / TILE);
    int bottomTile = std::floor(bottom / TILE);
    if (p.vy > 0) { // falling
        for (int ty = bottomTile; ty <= bottomTile; ++ty) {
            for (int tx = leftTile; tx <= rightTile; ++tx) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    p.py = ty * TILE - p.h; p.vy = 0; return;
                }
            }
        }
    } else if (p.vy < 0) { // rising
        for (int ty = topTile; ty >= topTile; --ty) {
            for (int tx = leftTile; tx <= rightTile; ++tx) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    p.py = (ty+1) * TILE; p.vy = 0; return;
                }
            }
        }
    }
    p.py = newPy;
}

// Enemy simple con tipos: ZOMBIE, SKELETON, SPIDER, CREEPER
struct Enemy {
    enum Type { ZOMBIE=0, SKELETON=1, SPIDER=2, CREEPER=3 } type;
    float x, y;
    float vx, vy;
    float w, h;
    int dir; // dirección horizontal preferida (-1 o 1)
    float moveSpeed;
    float pauseTimer; // tiempo de pausa para comportamiento torpe
    // creeper-specific
    float fuseTimer; // >0 means about to explode
    bool alive;
    int hp; // health points
    int maxHp;
    float respawnTimer; // seconds until respawn when dead
    int spawnTileX, spawnTileY; // where to respawn (tile coords)
};

void resolveHorizontalEnemy(World &world, Enemy &e, float newX) {
    float left = newX;
    float right = newX + e.w - 1;
    int topTile = std::floor(e.y / TILE);
    int bottomTile = std::floor((e.y + e.h - 1) / TILE);
    int leftTile = std::floor(left / TILE);
    int rightTile = std::floor(right / TILE);
    if (e.vx > 0) {
        for (int tx = rightTile; tx <= rightTile; ++tx) {
            for (int ty = topTile; ty <= bottomTile; ++ty) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    e.x = tx * TILE - e.w; e.vx = 0; return;
                }
            }
        }
    } else if (e.vx < 0) {
        for (int tx = leftTile; tx >= leftTile; --tx) {
            for (int ty = topTile; ty <= bottomTile; ++ty) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    e.x = (tx+1) * TILE; e.vx = 0; return;
                }
            }
        }
    }
    e.x = newX;
}

void resolveVerticalEnemy(World &world, Enemy &e, float newY) {
    float top = newY;
    float bottom = newY + e.h - 1;
    int leftTile = std::floor(e.x / TILE);
    int rightTile = std::floor((e.x + e.w - 1) / TILE);
    int topTile = std::floor(top / TILE);
    int bottomTile = std::floor(bottom / TILE);
    if (e.vy > 0) { // falling
        for (int ty = bottomTile; ty <= bottomTile; ++ty) {
            for (int tx = leftTile; tx <= rightTile; ++tx) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    e.y = ty * TILE - e.h; e.vy = 0; return;
                }
            }
        }
    } else if (e.vy < 0) { // rising
        for (int ty = topTile; ty >= topTile; --ty) {
            for (int tx = leftTile; tx <= rightTile; ++tx) {
                if (in_bounds(tx,ty) && isSolid(get_block(world,tx,ty))) {
                    e.y = (ty+1) * TILE; e.vy = 0; return;
                }
            }
        }
    }
    e.y = newY;
}

int main(){
    World world;
    init_world(world);

    Player p{};
    p.w = TILE-6; p.h = TILE-6;
    p.px = (W/2) * TILE; p.vx = 0; p.vy = 0; p.fx = 1; p.fy = 0; p.selected = (char)GRASS;
    // spawn player above surface at middle column
    int spawnTileY = 0;
    for (int y = 0; y < H; ++y) {
        if (get_block(world, W/2, y) != (char)AIR) { spawnTileY = y - 1; break; }
    }
    if (spawnTileY < 0) spawnTileY = H - 6;
    p.py = spawnTileY * TILE;
    // store spawn position for respawn on death
    float spawnPx = p.px;
    float spawnPy = p.py;
    p.inv[(char)GRASS]=10; p.inv[(char)DIRT]=8; p.inv[(char)STONE]=6; p.inv[(char)WOOD]=3; p.inv[(char)BEDR]=0;
    p.inv[(char)LEAF]=0; p.inv[(char)COAL]=0; p.inv[(char)IRON]=0; p.inv[(char)GOLD]=0;
    // make new biome/nether blocks placeable
    p.inv[(char)SAND] = 10;
    p.inv[(char)SNOW] = 8;
    p.inv[(char)NETH] = 2;
    p.inv[(char)LAVA] = 1;
    // herramientas iniciales
    p.tools["pickaxe"] = 1;
    p.tools["axe"] = 1;
    p.tools["shovel"] = 1;
    p.tools["sword"] = 1;
    p.selectedTool = "";

    // Player health
    const int MAX_HEALTH = 5;
    int playerHealth = MAX_HEALTH;
    float playerInvuln = 0.0f; // seconds remaining
    // fall damage / ground tracking
    bool wasOnGround = true;
    int lastGroundTile = static_cast<int>(std::floor((p.py + p.h) / TILE));
    int fallStartTile = lastGroundTile;
    // health regeneration
    float regenTimer = 0.0f;
    const float REGEN_INTERVAL = 8.0f; // seconds to recover 1 heart (faster)
    const float REGEN_DELAY_AFTER_DAMAGE = 5.0f; // wait after last damage before regen (faster)
    float timeSinceDamage = REGEN_DELAY_AFTER_DAMAGE; // seconds since last damage

    // Ventana ajustada a 1280x720: calculamos tiles visibles y usamos una cámara que sigue al jugador
    const int VIEW_W_TILES = 40; // 1280 / 32
    const int VIEW_H_TILES = 20; // (720 - HUD) / 32
    const int HUD_HEIGHT = 100; // larger HUD
    sf::RenderWindow window(sf::VideoMode(1280, 720), "Minecraft2D - SFML (Fisicas)");
    window.setFramerateLimit(60);
    sf::View camera(sf::FloatRect(0.f, 0.f, (float)VIEW_W_TILES * TILE, (float)VIEW_H_TILES * TILE));
    // Camera options: zoom out a bit to see more, and enable smoothing (LERP)
    const float CAM_ZOOM = 1.40f; // >1 zooms out (shows more) - alejamos la vista un poco más
    const float CAM_LERP = 8.0f; // smoothing speed
    camera.zoom(CAM_ZOOM);

    std::map<char, sf::Color> color {
        {(char)AIR, sf::Color(135,206,235)},
        {(char)GRASS, sf::Color(88, 166, 72)},
        {(char)SAND, sf::Color(194,178,128)},
        {(char)SNOW, sf::Color(235,245,255)},
        {(char)NETH, sf::Color(120,30,30)},
        {(char)LAVA, sf::Color(255,120,20)},
        {(char)DIRT, sf::Color(134, 96, 67)},
        {(char)STONE, sf::Color(120,120,120)},
        {(char)WOOD, sf::Color(150, 111, 51)},
        {(char)BEDR, sf::Color(40,40,40)},
        {(char)LEAF, sf::Color(110,180,80)},
        {(char)COAL, sf::Color(30,30,30)},
        {(char)IRON, sf::Color(180,180,200)},
        {(char)GOLD, sf::Color(212,175,55)}
    };

    // Friendly display names for blocks and tools (used in HUD)
    std::map<char, std::string> blockNames {
        {(char)GRASS, "Hierba"}, {(char)DIRT, "Tierra"}, {(char)STONE, "Piedra"}, {(char)WOOD, "Madera"}, {(char)LEAF, "Hoja"},
        {(char)COAL, "Carbón"}, {(char)IRON, "Hierro"}, {(char)GOLD, "Oro"}, {(char)SAND, "Arena"}, {(char)SNOW, "Nieve"},
        {(char)NETH, "Neth"}, {(char)LAVA, "Lava"}
    };
    std::map<std::string, std::string> toolNames {
        {"pickaxe", "Pico"}, {"axe", "Hacha"}, {"shovel", "Pala"}, {"sword", "Espada"}
    };

    sf::Font font;
    font.loadFromFile("assets/fonts/Minecraft.ttf");
    // Cargar texturas desde assets/images (si existen)
    namespace fs = std::filesystem;
    std::map<std::string, sf::Texture> textures;
    if (fs::exists("assets/images")) {
        for (auto &ent : fs::directory_iterator("assets/images")) {
            if (!ent.is_regular_file()) continue;
            std::string path = ent.path().string();
            std::string stem = ent.path().stem().string();
            sf::Texture tex;
            if (tex.loadFromFile(path)) {
                textures[stem] = std::move(tex);
            }
        }
    }

    // Música de fondo: escoger un archivo aleatorio de assets/music si hay
    sf::Music bgm;
    std::vector<std::string> musicFiles;
    // damage sound buffer (Danio)
    sf::SoundBuffer damageBuf;
    sf::Sound damageSound;
    bool hasDamageSound = false;
    if (fs::exists("assets/music")) {
        for (auto &ent : fs::directory_iterator("assets/music")) {
            if (!ent.is_regular_file()) continue;
            std::string ext = ent.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext==".ogg" || ext==".wav" || ext==".flac" || ext==".mp3") {
                std::string p = ent.path().string();
                std::string stem = ent.path().stem().string();
                // if the file is named Danio (case-insensitive) use it as damage sound
                std::string lowerStem = stem; std::transform(lowerStem.begin(), lowerStem.end(), lowerStem.begin(), ::tolower);
                if (lowerStem == "danio") {
                    if (damageBuf.loadFromFile(p)) { damageSound.setBuffer(damageBuf); hasDamageSound = true; }
                } else {
                    musicFiles.push_back(p);
                }
            }
        }
    }
    if (!musicFiles.empty()) {
        int idx = std::rand() % (int)musicFiles.size();
        if (bgm.openFromFile(musicFiles[idx])) { bgm.setLoop(true); bgm.setVolume(40); bgm.play(); }
        else std::cerr << "Aviso: no pude abrir " << musicFiles[idx] << std::endl;
    } else {
        std::cerr << "Aviso: carpeta 'assets/music' vacía o inexistente." << std::endl;
    }

    sf::RectangleShape tileShape(sf::Vector2f(TILE, TILE));
    sf::RectangleShape playerShape(sf::Vector2f(p.w, p.h));
    playerShape.setFillColor(sf::Color::Yellow);
    // sprites si hay texturas
    sf::Sprite playerSprite;
    bool playerHasTexture = false;
    if (textures.count("player")) { playerSprite.setTexture(textures["player"]); playerHasTexture = true; }
    if (playerHasTexture) {
        auto &t = textures["player"];
        if (t.getSize().x > 0 && t.getSize().y > 0) playerSprite.setScale(p.w / (float)t.getSize().x, p.h / (float)t.getSize().y);
    }

    // FPS display
    sf::Text fpsText;
    fpsText.setFont(font);
    fpsText.setCharacterSize(14);
    fpsText.setFillColor(sf::Color::White);

    // Crear varios enemigos: zombi, esqueleto, araña y creeper
    std::vector<Enemy> enemies;
    auto spawnEnemyAt = [&](Enemy::Type t, int tileXOffset){
        // spawn only in caves: search for an underground tile near center+offset
        int baseX = std::min(W-2, W/2 + tileXOffset);
        // find surface height at baseX
        int surfaceY = 0;
        for (int y=0;y<H;++y) { if (get_block(world, baseX, y) != (char)AIR) { surfaceY = y; break; } }
        // search nearby columns for a cave floor (air tile with solid tile below and y > surfaceY + 2)
        int foundX=-1, foundY=-1;
        for (int dx=-8; dx<=8 && foundX==-1; ++dx) {
            int cx = baseX + dx; if (cx < 1 || cx > W-2) continue;
            for (int y = surfaceY + 3; y < H-2; ++y) {
                if (get_block(world, cx, y) == (char)AIR && isSolid(get_block(world, cx, y+1))) { foundX = cx; foundY = y; break; }
            }
        }
        if (foundX == -1) return; // no cave found nearby
        Enemy e{};
        e.type = t; e.w = p.w; e.h = p.h; e.vx = 0; e.vy = 0; e.dir = (std::rand()%2)?1:-1; e.moveSpeed = 60.0f; e.pauseTimer = 0.0f; e.fuseTimer = 0.0f; e.alive = true;
        e.x = foundX * TILE; e.y = (foundY - 1) * TILE; // stand on the block above the floor AIR
        e.spawnTileX = foundX; e.spawnTileY = foundY - 1;
        e.respawnTimer = 0.0f;
        // set HP by type
        if (t == Enemy::ZOMBIE) { e.maxHp = 2; }
        else { e.maxHp = 1; }
        e.hp = e.maxHp;
        // tweak per type
        if (t == Enemy::SPIDER) { e.moveSpeed = 80.0f; }
        if (t == Enemy::CREEPER) { e.moveSpeed = 30.0f; }
        if (t == Enemy::SKELETON) { e.moveSpeed = 60.0f; }
        enemies.push_back(e);
    };
    spawnEnemyAt(Enemy::ZOMBIE, 6);
    spawnEnemyAt(Enemy::SKELETON, -6);
    spawnEnemyAt(Enemy::SPIDER, 10);
    spawnEnemyAt(Enemy::CREEPER, -10);

    sf::RectangleShape enemyShape(sf::Vector2f(p.w, p.h));

    const float GRAVITY = 1500.0f; // px/s^2
    const float MOVE_SPEED = 150.0f; // px/s
    const float JUMP_SPEED = 520.0f; // px/s
    // Sword (attack) mechanics
    const float SWING_RANGE = 64.0f; // px (increased reach)
    const float SWING_COOLDOWN = 0.5f; // s (quicker swings)
    const float SWING_ACTIVE = 0.15f; // s (shorter hit window)
    float swingTimer = 0.0f;
    float swingActive = 0.0f;
    const float ENEMY_RESPAWN_BASE = 8.0f; // base seconds before enemy can respawn (faster)
    const float ENEMY_RESPAWN_VAR = 4.0f; // random additional seconds (0..VAR)
    const int SWORD_DAMAGE = 1; // damage per hit
    const float DAY_LENGTH = 120.0f; // seconds for full day-night cycle
    float dayTime = 0.0f;
    const float PI = 3.14159265358979323846f;

    // Weather system
    enum WeatherMode { WEATHER_NONE = 0, WEATHER_RAIN = 1, WEATHER_SNOW = 2 };
    int weatherMode = WEATHER_NONE;
    struct WeatherParticle { float x; float y; float vy; float life; bool snow; };
    std::vector<WeatherParticle> weatherParticles;
    const float WEATHER_RAIN_SPAWN_PER_SEC = 180.0f; // spawn rate per second per screen
    const float WEATHER_SNOW_SPAWN_PER_SEC = 60.0f;
    float weatherSpawnAcc = 0.0f;
    // Effect particles (sparks, explosion debris)
    struct EffectParticle { float x; float y; float vx; float vy; float life; float size; sf::Color col; };
    std::vector<EffectParticle> effectParticles;

    sf::Clock clock;
    // Picar bloques por tiempo
    bool breaking = false;
    int breakX = -1, breakY = -1;
    float breakProgress = 0.0f;
    const float BASE_BREAK_TIME = 0.6f; // segundos base (ligeramente más rápido)
    bool prevMouseLeft = false; // for edge detection of left click
    bool showBlockPicker = false; // F toggles a block selection overlay
    bool showHelp = false; // H toggles help panel
    const int INV_SLOTS = 12; // inventory slots shown at bottom
    while (window.isOpen()){
        sf::Event ev;
        while (window.pollEvent(ev)){
            if (ev.type == sf::Event::Closed) window.close();
            if (ev.type == sf::Event::KeyPressed){
                if (ev.key.code == sf::Keyboard::Escape) window.close();
                if (ev.key.code == sf::Keyboard::Num1) { p.selected=(char)GRASS; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num2) { p.selected=(char)DIRT; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num3) { p.selected=(char)STONE; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num4) { p.selected=(char)WOOD; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num5) { p.selected=(char)LEAF; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num6) { p.selected=(char)COAL; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num7) { p.selected=(char)IRON; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num8) { p.selected=(char)GOLD; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num9) { p.selected=(char)SAND; showBlockPicker=false; }
                if (ev.key.code == sf::Keyboard::Num0) { p.selected=(char)SNOW; showBlockPicker=false; }
                // tecla X ahora inicia picar (mecánica por tiempo) — manejado en el bucle principal
                if (ev.key.code == sf::Keyboard::C) {
                    int centerX = static_cast<int>(p.px + p.w/2);
                    int centerY = static_cast<int>(p.py + p.h/2);
                    int tx = (centerX + p.fx * TILE) / TILE;
                    int ty = (centerY + p.fy * TILE) / TILE;
                    char b = p.selected;
                    if (in_bounds(tx,ty) && get_block(world,tx,ty)==(char)AIR && p.inv[b]>0){ p.inv[b]--; set_block(world,tx,ty,b); }
                }
                if (ev.key.code == sf::Keyboard::W || ev.key.code == sf::Keyboard::Space || ev.key.code == sf::Keyboard::Up) {
                    // Salto: solo si estamos sobre suelo (pequeña comprobación)
                    int belowTileY = static_cast<int>(std::floor((p.py + p.h + 1) / TILE));
                    int leftTile = static_cast<int>(std::floor(p.px / TILE));
                    int rightTile = static_cast<int>(std::floor((p.px + p.w -1) / TILE));
                    bool onGround = false;
                    for (int tx = leftTile; tx <= rightTile; ++tx) if (in_bounds(tx,belowTileY) && isSolid(get_block(world,tx,belowTileY))) onGround = true;
                    if (onGround) { p.vy = -JUMP_SPEED; }
                }
                // tools: Q=pickaxe, E=axe, R=shovel
                if (ev.key.code == sf::Keyboard::Q) { if (p.tools["pickaxe"]>0) p.selectedTool = "pickaxe"; else p.selectedTool = ""; }
                if (ev.key.code == sf::Keyboard::E) { if (p.tools["axe"]>0) p.selectedTool = "axe"; else p.selectedTool = ""; }
                if (ev.key.code == sf::Keyboard::R) { if (p.tools["shovel"]>0) p.selectedTool = "shovel"; else p.selectedTool = ""; }
                if (ev.key.code == sf::Keyboard::T) { if (p.tools["sword"]>0) p.selectedTool = "sword"; else p.selectedTool = ""; }
                if (ev.key.code == sf::Keyboard::F) { showBlockPicker = !showBlockPicker; }
                if (ev.key.code == sf::Keyboard::K) {
                    // cycle weather: none -> rain -> snow -> none
                    weatherMode = (weatherMode + 1) % 3;
                    weatherParticles.clear();
                }
                if (ev.key.code == sf::Keyboard::H) {
                    showHelp = !showHelp;
                }
                if (ev.key.code == sf::Keyboard::F) {
                    // sword attack
                    // only swing if sword is selected
                    if (p.selectedTool == "sword" && p.tools["sword"]>0) {
                        if (swingTimer <= 0.0f) { swingTimer = SWING_COOLDOWN; swingActive = SWING_ACTIVE; }
                    }
                }
            }
            if (ev.type == sf::Event::MouseButtonPressed){
                // click handling: colocar con botón derecho (inmediato). Picar con botón izquierdo ahora se maneja manteniendo pulsado (ver loop principal).
                sf::Vector2i m = sf::Vector2i(ev.mouseButton.x, ev.mouseButton.y);
                // overlay block picker handling (default view coords)
                if (showBlockPicker && ev.mouseButton.button == sf::Mouse::Left) {
                    sf::Vector2f hudPos = window.mapPixelToCoords(m, window.getDefaultView());
                    // layout
                    int cols = 4;
                    int rows = (INV_SLOTS + cols - 1) / cols;
                    float slotW = 80.0f, slotH = 80.0f, gap = 12.0f;
                    float panelW = cols * slotW + (cols-1)*gap;
                    float panelH = rows * slotH + (rows-1)*gap;
                    sf::Vector2f center((float)VIEW_W_TILES * TILE * 0.5f, (float)VIEW_H_TILES * TILE * 0.5f);
                    float startX = center.x - panelW*0.5f; float startY = center.y - panelH*0.5f;
                    std::vector<char> picker = {(char)GRASS,(char)DIRT,(char)STONE,(char)WOOD,(char)LEAF,(char)COAL,(char)IRON,(char)GOLD,(char)SAND,(char)SNOW,(char)NETH,(char)LAVA};
                    for (int i = 0; i < INV_SLOTS; ++i) {
                        int r = i / cols; int c = i % cols;
                        float sx = startX + c * (slotW + gap);
                        float sy = startY + r * (slotH + gap);
                        sf::FloatRect rect(sx, sy, slotW, slotH);
                        if (hudPos.x >= rect.left && hudPos.x <= rect.left + rect.width && hudPos.y >= rect.top && hudPos.y <= rect.top + rect.height) {
                            if (i < (int)picker.size()) { p.selected = picker[i]; }
                            showBlockPicker = false;
                            break;
                        }
                    }
                    continue;
                }
                // check clicks on HUD inventory (default view coords)
                sf::Vector2f hudPos = window.mapPixelToCoords(m, window.getDefaultView());
                // inventory slots are at y = VIEW_H_TILES * TILE + 16, slots width 48, stride 60, start x=10
                if (ev.mouseButton.button == sf::Mouse::Left) {
                    float invY = (float)VIEW_H_TILES * TILE + 16.0f;
                    if (hudPos.y >= invY && hudPos.y <= invY + 48.0f) {
                        int relX = static_cast<int>(hudPos.x - 10.0f);
                        if (relX >= 0) {
                            int idx = relX / 60;
                            if (idx >= 0 && idx < INV_SLOTS) {
                                std::vector<char> mapSel = {(char)GRASS,(char)DIRT,(char)STONE,(char)WOOD,(char)LEAF,(char)COAL,(char)IRON,(char)GOLD,(char)SAND,(char)SNOW,(char)NETH,(char)LAVA};
                                p.selected = mapSel[idx];
                                // consume this click for HUD selection
                                continue;
                            }
                        }
                    }
                }
                // mapear la posición del ratón a coordenadas del mundo según la cámara
                sf::Vector2f worldPos = window.mapPixelToCoords(m, camera);
                int mx = static_cast<int>(std::floor(worldPos.x)) / TILE; int my = static_cast<int>(std::floor(worldPos.y)) / TILE;
                if (ev.mouseButton.button == sf::Mouse::Right){
                    if (in_bounds(mx,my)){
                        char b = p.selected;
                        if (get_block(world,mx,my)==(char)AIR && p.inv[b]>0){ p.inv[b]--; set_block(world,mx,my,b); }
                    }
                }
            }
        }

        float dt = clock.restart().asSeconds();
        // advance day-night time
        dayTime += dt;
        float phase = std::fmod(dayTime, DAY_LENGTH) / DAY_LENGTH; // 0..1
        float sun = 0.5f + 0.5f * std::sin(phase * 2.0f * PI); // -? maps 0..1
        float ambient = 0.4f + 0.6f * sun; // 0.4..1.0
        // sky color: lerp between night and day using sun
        sf::Color daySky(135,206,235);
        sf::Color nightSky(10,10,40);
        auto lerpC = [&](const sf::Color &a, const sf::Color &b, float t){ return sf::Color((sf::Uint8)(a.r * t + b.r * (1.0f-t)), (sf::Uint8)(a.g * t + b.g * (1.0f-t)), (sf::Uint8)(a.b * t + b.b * (1.0f-t))); };
        sf::Color skyColor = lerpC(daySky, nightSky, 1.0f - sun);

        // update swing timers
        if (swingTimer > 0.0f) swingTimer = std::max(0.0f, swingTimer - dt);
        if (swingActive > 0.0f) swingActive = std::max(0.0f, swingActive - dt);

        // Input horizontal
        float targetVx = 0;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::A) || sf::Keyboard::isKeyPressed(sf::Keyboard::Left)) { targetVx = -MOVE_SPEED; p.fx = -1; }
        else if (sf::Keyboard::isKeyPressed(sf::Keyboard::D) || sf::Keyboard::isKeyPressed(sf::Keyboard::Right)) { targetVx = MOVE_SPEED; p.fx = 1; }
        else { targetVx = 0; }
        p.vx = targetVx;

        // Apply gravity
        p.vy += GRAVITY * dt;
        if (p.vy > 2000.0f) p.vy = 2000.0f;

        // Move horizontally and resolve collisions
        float newPx = p.px + p.vx * dt;
        resolveHorizontal(world, p, newPx);

        // Move vertically and resolve collisions
        float newPy = p.py + p.vy * dt;
        resolveVertical(world, p, newPy);

        // update facing y
        p.fy = (p.vy > 0) ? 1 : (p.vy < 0 ? -1 : 0);

        // Fall damage detection: check landing and start-fall
        int leftTile = static_cast<int>(std::floor(p.px / TILE));
        int rightTile = static_cast<int>(std::floor((p.px + p.w -1) / TILE));
        int belowTileY = static_cast<int>(std::floor((p.py + p.h + 1) / TILE));
        bool onGround = false;
        for (int tx = leftTile; tx <= rightTile; ++tx) if (in_bounds(tx,belowTileY) && isSolid(get_block(world,tx,belowTileY))) onGround = true;
        if (!wasOnGround && onGround) {
            // landed
            int landingTile = belowTileY;
            int dropTiles = landingTile - fallStartTile;
            if (dropTiles >= 5 && playerInvuln <= 0.0f) {
                playerHealth = std::max(0, playerHealth - 1);
                playerInvuln = 1.0f;
                timeSinceDamage = 0.0f;
                if (hasDamageSound) damageSound.play();
            }
        }
        if (wasOnGround && !onGround) {
            // started falling: record the ground tile we left
            fallStartTile = lastGroundTile;
        }
        if (onGround) lastGroundTile = belowTileY;
        wasOnGround = onGround;

        // --- Mecánica de picar por tiempo / ataque con clic izquierdo ---
        bool keyBreak = sf::Keyboard::isKeyPressed(sf::Keyboard::X);
        bool curMouseLeft = sf::Mouse::isButtonPressed(sf::Mouse::Left);
        bool mousePressedThisFrame = (curMouseLeft && !prevMouseLeft);
        // if sword is selected, left-click triggers attack on press instead of mining
        bool mouseBreak = false;
        if (curMouseLeft) {
            if (p.selectedTool == "sword" && p.tools["sword"]>0) {
                mouseBreak = false; // do not mine while sword held
            } else {
                mouseBreak = true;
            }
        }
        int targetX = -1, targetY = -1;
        if (keyBreak) {
            int centerX = static_cast<int>(p.px + p.w/2);
            int centerY = static_cast<int>(p.py + p.h/2);
            targetX = (centerX + p.fx * TILE) / TILE;
            targetY = (centerY + p.fy * TILE) / TILE;
        } else if (mouseBreak) {
            sf::Vector2i mpos = sf::Mouse::getPosition(window);
            sf::Vector2f wp = window.mapPixelToCoords(mpos, camera);
            targetX = static_cast<int>(std::floor(wp.x)) / TILE; targetY = static_cast<int>(std::floor(wp.y)) / TILE;
        }

        if (targetX != -1 && in_bounds(targetX, targetY)) {
            char tb = get_block(world, targetX, targetY);
            if (tb != (char)AIR && tb != (char)BEDR) {
                // determine break time modifier by block type
                float mult = 1.0f;
                if (tb == (char)STONE) mult = 2.0f;
                else if (tb == (char)WOOD) mult = 0.8f;
                else if (tb == (char)LEAF) mult = 0.4f;
                else if (tb == (char)COAL) mult = 1.2f;
                else if (tb == (char)IRON) mult = 3.0f;
                else if (tb == (char)GOLD) mult = 4.0f;

                // tool modifiers: improved pickaxe/axe/shovel effectiveness
                if (p.selectedTool == "pickaxe" && p.tools["pickaxe"]>0) {
                    if (tb == (char)STONE || tb == (char)IRON || tb == (char)GOLD || tb == (char)COAL) mult *= 0.45f;
                }
                if (p.selectedTool == "axe" && p.tools["axe"]>0) {
                    if (tb == (char)WOOD || tb == (char)LEAF) mult *= 0.45f;
                }
                if (p.selectedTool == "shovel" && p.tools["shovel"]>0) {
                    if (tb == (char)DIRT || tb == (char)SAND) mult *= 0.45f;
                }

                if (breaking && breakX == targetX && breakY == targetY) {
                    breakProgress += dt;
                } else {
                    breaking = true;
                    breakX = targetX; breakY = targetY; breakProgress = dt;
                }

                float need = BASE_BREAK_TIME * mult;
                if (breakProgress >= need) {
                    // completar ruptura
                    p.inv[tb]++;
                    set_block(world, breakX, breakY, (char)AIR);
                    breaking = false; breakX = breakY = -1; breakProgress = 0.0f;
                }
            } else {
                // objetivo no picable
                breaking = false; breakX = breakY = -1; breakProgress = 0.0f;
            }
        } else {
            // no está picando
            breaking = false; breakX = breakY = -1; breakProgress = 0.0f;
        }

        // Actualizar enemigos (solo procesar IA/colisiones cuando estén cerca para mejorar rendimiento)
        for (auto &e : enemies) {
            // always decrement respawn timers for dead ones
            if (!e.alive) {
                if (e.respawnTimer > 0.0f) e.respawnTimer = std::max(0.0f, e.respawnTimer - dt);
                // try respawn when timer reaches 0 (handled below in common code)
            } else {
                // if alive, only process when close to player
                float exCenter = e.x + e.w*0.5f;
                float pxCenter = p.px + p.w*0.5f;
                float dxE = pxCenter - exCenter;
                float dyE = (p.py + p.h*0.5f) - (e.y + e.h*0.5f);
                float dist = std::hypot(dxE, dyE);
                const float ACTIVE_RANGE = 1200.0f; // px
                if (dist < ACTIVE_RANGE) {
                    e.vy += GRAVITY * dt;
                    if (e.vy > 2000.0f) e.vy = 2000.0f;

                    float distE = std::abs(dxE);
                    if (e.pauseTimer > 0.0f) { e.pauseTimer -= dt; e.vx = 0.0f; }
                    else {
                        if (e.type == Enemy::ZOMBIE || e.type == Enemy::SKELETON) {
                            if (distE < 500.0f) e.vx = (dxE > 0.0f) ? e.moveSpeed : -e.moveSpeed;
                            else { e.vx = e.moveSpeed * e.dir; if ((std::rand() % 1000) < 8) { e.dir = -e.dir; e.pauseTimer = 0.35f; e.vx = 0.0f; } }
                        } else if (e.type == Enemy::SPIDER) {
                            // spider: can jump higher towards player
                            int belowTileY = static_cast<int>(std::floor((e.y + e.h + 1) / TILE));
                            int leftTile = static_cast<int>(std::floor(e.x / TILE));
                            int rightTile = static_cast<int>(std::floor((e.x + e.w -1) / TILE));
                            bool onGround = false;
                            for (int tx = leftTile; tx <= rightTile; ++tx) if (in_bounds(tx,belowTileY) && isSolid(get_block(world,tx,belowTileY))) onGround = true;
                            if (distE < 500.0f) e.vx = (dxE > 0.0f) ? e.moveSpeed : -e.moveSpeed;
                            else e.vx = e.moveSpeed * e.dir;
                            if (onGround && distE < 250.0f && (std::rand()%100) < 25) { e.vy = -JUMP_SPEED * 1.15f; }
                        } else if (e.type == Enemy::CREEPER) {
                            // creeper: slow approach, when close start fuse and explode
                            const float triggerDist = 160.0f;
                            if (distE < triggerDist && e.fuseTimer <= 0.0f) { e.fuseTimer = 1.6f; }
                            if (e.fuseTimer > 0.0f) { e.fuseTimer -= dt; if (e.fuseTimer <= 0.0f) {
                                // explode: clear nearby blocks (2-tile radius)
                                int radiusTiles = 2;
                                int cx = static_cast<int>(std::floor((e.x + e.w*0.5f) / TILE));
                                int cy = static_cast<int>(std::floor((e.y + e.h*0.5f) / TILE));
                                for (int oy = -radiusTiles; oy <= radiusTiles; ++oy) for (int ox = -radiusTiles; ox <= radiusTiles; ++ox) {
                                    int bx = cx + ox; int by = cy + oy;
                                    if (in_bounds(bx,by) && get_block(world,bx,by)!=(char)BEDR) set_block(world,bx,by,(char)AIR);
                                }
                                // spawn explosion effect particles and camera shake
                                float ex = e.x + e.w*0.5f; float ey = e.y + e.h*0.5f;
                                for (int pi = 0; pi < 20; ++pi) {
                                    EffectParticle ep; ep.x = ex; ep.y = ey; ep.vx = (std::rand()%200 - 100) * 3.0f; ep.vy = (std::rand()%200 - 200) * 3.0f; ep.life = 0.8f + (std::rand()%100)/200.0f; ep.size = 2.0f + (std::rand()%6); ep.col = (pi%2==0) ? sf::Color(255,180,60) : sf::Color(180,80,40); effectParticles.push_back(ep);
                                }
                                // damage player if inside explosion
                                float edist = std::hypot((pxCenter - ex), ((p.py + p.h*0.5f) - ey));
                                if (edist < (radiusTiles * TILE + 8.0f) && playerInvuln <= 0.0f) { playerHealth = std::max(0, playerHealth - 1); playerInvuln = 1.0f; timeSinceDamage = 0.0f; if (hasDamageSound) damageSound.play(); }
                                e.alive = false; e.vx = e.vy = 0.0f;
                                // randomized respawn time
                                e.respawnTimer = ENEMY_RESPAWN_BASE + (std::rand() % ((int)ENEMY_RESPAWN_VAR + 1));
                            } }
                            // approach slowly while not fusing
                            if (e.fuseTimer <= 0.0f) {
                                if (distE < 500.0f) e.vx = (dxE > 0.0f) ? e.moveSpeed : -e.moveSpeed; else e.vx = e.moveSpeed * e.dir;
                            } else e.vx = 0.0f; // fuse pause movement
                        }
                    }

                    float newEx = e.x + e.vx * dt;
                    resolveHorizontalEnemy(world, e, newEx);
                    float newEy = e.y + e.vy * dt;
                    resolveVerticalEnemy(world, e, newEy);

                    // collision damage to player (creeper handled on explosion)
                    if (playerInvuln <= 0.0f && e.alive && e.type != Enemy::CREEPER) {
                        float ax1 = e.x, ay1 = e.y, ax2 = e.x + e.w, ay2 = e.y + e.h;
                        float bx1 = p.px, by1 = p.py, bx2 = p.px + p.w, by2 = p.py + p.h;
                        bool overlap = (ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1);
                        if (overlap) { playerHealth = std::max(0, playerHealth - 1); playerInvuln = 1.0f; timeSinceDamage = 0.0f; if (hasDamageSound) damageSound.play(); }
                    }
                } // end if dist < ACTIVE_RANGE
            }
            // handle dead enemies respawn timer (improved): postpone if player nearby and choose safe nearby spot
            if (!e.alive) {
                if (e.respawnTimer > 0.0f) {
                    // already decremented above maybe; keep safe
                } else if (e.respawnTimer <= 0.0f) {
                    // avoid respawn if player is very close to spawn
                    float spawnCx = e.spawnTileX * TILE + TILE*0.5f;
                    float spawnCy = e.spawnTileY * TILE + TILE*0.5f;
                    float pxCenter = p.px + p.w*0.5f; float pyCenter = p.py + p.h*0.5f;
                    float pdist = std::hypot(pxCenter - spawnCx, pyCenter - spawnCy);
                    if (pdist < 5.0f * TILE) {
                        // push respawn a bit further
                        e.respawnTimer = 2.0f + (std::rand() % 3);
                    } else {
                        bool placed = false;
                        // search for a nearby suitable tile (air with solid below)
                        for (int r = 0; r <= 6 && !placed; ++r) {
                            for (int dx = -r; dx <= r && !placed; ++dx) for (int dy = -r; dy <= r && !placed; ++dy) {
                                int tx = e.spawnTileX + dx; int ty = e.spawnTileY + dy;
                                if (!in_bounds(tx, ty)) continue;
                                if (get_block(world, tx, ty) == (char)AIR && isSolid(get_block(world, tx, ty+1))) {
                                    e.x = tx * TILE; e.y = ty * TILE; e.alive = true; e.hp = e.maxHp; e.vx = 0.0f; e.vy = 0.0f; e.fuseTimer = 0.0f; e.pauseTimer = 0.8f; placed = true; break;
                                }
                            }
                        }
                        if (!placed) {
                            // fallback: respawn at exact spawn tile
                            e.x = e.spawnTileX * TILE; e.y = e.spawnTileY * TILE; e.alive = true; e.hp = e.maxHp; e.vx = 0.0f; e.vy = 0.0f; e.fuseTimer = 0.0f; e.pauseTimer = 0.8f;
                        }
                    }
                }
            }
        }

        // Sword hit detection while swingActive > 0
        if (swingActive > 0.0f) {
            float attackX = (p.fx >= 0) ? (p.px + p.w) : (p.px - SWING_RANGE);
            float attackY = p.py;
            float attackW = SWING_RANGE;
            float attackH = p.h;
            for (auto &e : enemies) {
                if (!e.alive) continue;
                float ax1 = attackX, ay1 = attackY, ax2 = attackX + attackW, ay2 = attackY + attackH;
                float bx1 = e.x, by1 = e.y, bx2 = e.x + e.w, by2 = e.y + e.h;
                bool hit = (ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1);
                if (hit) {
                    // only damage if sword is selected
                    if (p.selectedTool == "sword" && p.tools["sword"]>0) {
                        e.hp -= SWORD_DAMAGE;
                        // spawn hit sparks
                        for (int si = 0; si < 6; ++si) {
                            EffectParticle ep; ep.x = e.x + e.w*0.5f; ep.y = e.y + e.h*0.5f; ep.vx = (std::rand()%200 - 100) * 2.0f; ep.vy = (std::rand()%200 - 200) * 2.0f; ep.life = 0.25f + (std::rand()%100)/400.0f; ep.size = 1.0f + (std::rand()%3); ep.col = sf::Color(255,220,160); effectParticles.push_back(ep);
                        }
                        if (e.hp <= 0) {
                            e.alive = false;
                            e.vx = e.vy = 0.0f;
                            e.respawnTimer = ENEMY_RESPAWN_BASE + (std::rand() % ((int)ENEMY_RESPAWN_VAR + 1));
                        }
                    }
                }
            }
        }

        // handle left-click attack trigger (edge): if pressed this frame and sword selected, trigger swing
        bool curMouseLeftForEdge = sf::Mouse::isButtonPressed(sf::Mouse::Left);
        if (curMouseLeftForEdge && !prevMouseLeft) {
            if (p.selectedTool == "sword" && p.tools["sword"]>0) {
                if (swingTimer <= 0.0f) { swingTimer = SWING_COOLDOWN; swingActive = SWING_ACTIVE; }
            }
        }
        prevMouseLeft = curMouseLeftForEdge;

        // actualizar invulnerabilidad del jugador
        if (playerInvuln > 0.0f) playerInvuln = std::max(0.0f, playerInvuln - dt);
        // actualizar timers de regeneración
        timeSinceDamage += dt;
        if (timeSinceDamage >= REGEN_DELAY_AFTER_DAMAGE) {
            regenTimer += dt;
            if (regenTimer >= REGEN_INTERVAL) {
                if (playerHealth < MAX_HEALTH) playerHealth++;
                regenTimer = 0.0f;
            }
        } else {
            regenTimer = 0.0f;
        }

        // Death / respawn
        if (playerHealth <= 0) {
            // respawn at initial spawn
            p.px = spawnPx; p.py = spawnPy; p.vx = 0.0f; p.vy = 0.0f;
            playerHealth = MAX_HEALTH;
            playerInvuln = 1.0f;
            timeSinceDamage = REGEN_DELAY_AFTER_DAMAGE; // delay regen after death
            // reset fall tracking
            wasOnGround = true;
            lastGroundTile = static_cast<int>(std::floor((p.py + p.h) / TILE));
            fallStartTile = lastGroundTile;
        }

        window.clear(skyColor);

        // actualizar cámara centrada en el jugador pero limitada al mapa
        float halfW = (float)VIEW_W_TILES * TILE * 0.5f * CAM_ZOOM;
        float halfH = (float)VIEW_H_TILES * TILE * 0.5f * CAM_ZOOM;
        float mapPixelW = (float)W * TILE;
        float mapPixelH = (float)H * TILE;
        float desiredX = p.px + p.w*0.5f;
        float desiredY = p.py + p.h*0.5f;
        float camX = std::min(std::max(desiredX, halfW), mapPixelW - halfW);
        float camY = std::min(std::max(desiredY, halfH), mapPixelH - halfH);
        // Smooth camera: interpolate current center towards desired using exponential smoothing
        sf::Vector2f curCenter = camera.getCenter();
        sf::Vector2f desiredCenter(camX, camY);
        float alpha = 1.0f - std::exp(-CAM_LERP * dt); // smoothing factor
        sf::Vector2f newCenter = curCenter + (desiredCenter - curCenter) * alpha;
        camera.setCenter(newCenter);

        // dibujamos el mundo usando la cámara (culling por vista)
        window.setView(camera);
        {
            sf::Vector2f c = camera.getCenter(); sf::Vector2f s = camera.getSize();
            float left = c.x - s.x*0.5f; float top = c.y - s.y*0.5f;
            int minX = std::max(0, (int)std::floor(left / TILE) - 1);
            int minY = std::max(0, (int)std::floor(top / TILE) - 1);
            int maxX = std::min(W-1, (int)std::ceil((left + s.x) / TILE) + 1);
            int maxY = std::min(H-1, (int)std::ceil((top + s.y) / TILE) + 1);
            for (int y=minY;y<=maxY;++y){
                for (int x=minX;x<=maxX;++x){
                    char b = get_block(world,x,y);
                    sf::Color baseCol = color.count(b) ? color[b] : sf::Color::Magenta;
                    sf::Color col((sf::Uint8)std::min(255.0f, baseCol.r * ambient), (sf::Uint8)std::min(255.0f, baseCol.g * ambient), (sf::Uint8)std::min(255.0f, baseCol.b * ambient));
                    tileShape.setPosition(x*TILE, y*TILE);
                    tileShape.setFillColor(col);
                    window.draw(tileShape);
                }
            }
        }

        // Weather particles: spawn and update (in world coordinates)
        {
            sf::Vector2f c = camera.getCenter(); sf::Vector2f s = camera.getSize();
            float left = c.x - s.x*0.5f; float top = c.y - s.y*0.5f;
            float right = left + s.x; float bottom = top + s.y;
            // spawn accumulator
            if (weatherMode == WEATHER_RAIN) {
                weatherSpawnAcc += dt * WEATHER_RAIN_SPAWN_PER_SEC;
                while (weatherSpawnAcc >= 1.0f) {
                    weatherSpawnAcc -= 1.0f;
                    WeatherParticle p0; p0.x = left + (std::rand() % (int)s.x); p0.y = top - 10.0f; p0.vy = 700.0f + (std::rand()%300); p0.life = (bottom - top) / p0.vy + 1.0f; p0.snow = false; weatherParticles.push_back(p0);
                }
            } else if (weatherMode == WEATHER_SNOW) {
                weatherSpawnAcc += dt * WEATHER_SNOW_SPAWN_PER_SEC;
                while (weatherSpawnAcc >= 1.0f) {
                    weatherSpawnAcc -= 1.0f;
                    WeatherParticle p0; p0.x = left + (std::rand() % (int)s.x); p0.y = top - 10.0f; p0.vy = 60.0f + (std::rand()%100); p0.life = (bottom - top) / p0.vy + 2.0f; p0.snow = true; weatherParticles.push_back(p0);
                }
            } else {
                // no spawn
            }
            // update particles
            for (int i = (int)weatherParticles.size()-1; i >= 0; --i) {
                auto &wp = weatherParticles[i];
                wp.y += wp.vy * dt;
                wp.life -= dt;
                if (wp.life <= 0.0f || wp.y > bottom + 20.0f) { weatherParticles.erase(weatherParticles.begin() + i); }
            }
            // draw particles
            for (auto &wp : weatherParticles) {
                if (wp.snow) {
                    sf::CircleShape cs(2.0f);
                    cs.setFillColor(sf::Color(240,240,255,220));
                    cs.setPosition(wp.x, wp.y);
                    window.draw(cs);
                } else {
                    sf::RectangleShape rs(sf::Vector2f(2.0f, 10.0f));
                    rs.setFillColor(sf::Color(160,200,255,200));
                    rs.setPosition(wp.x, wp.y);
                    window.draw(rs);
                }
            }
        }

        // Effect particles update & draw (sparks, explosion debris)
        for (int i = (int)effectParticles.size()-1; i >= 0; --i) {
            auto &ep = effectParticles[i];
            ep.x += ep.vx * dt; ep.y += ep.vy * dt; ep.vy += 800.0f * dt; // light gravity
            ep.life -= dt;
            if (ep.life <= 0.0f) { effectParticles.erase(effectParticles.begin() + i); continue; }
            sf::CircleShape cs(ep.size);
            sf::Color c = ep.col; float a = std::max(0.0f, ep.life);
            c.a = (sf::Uint8)(255.0f * std::min(1.0f, a));
            cs.setFillColor(c);
            cs.setPosition(ep.x, ep.y);
            window.draw(cs);
        }

        // mostrar progreso de picar si aplica (en coordenadas del mundo, con la cámara activa)
        if (breaking && breakX>=0 && breakY>=0) {
            sf::RectangleShape overlay(sf::Vector2f(TILE, TILE));
            overlay.setPosition(breakX * TILE, breakY * TILE);
            overlay.setFillColor(sf::Color(0,0,0,80));
            window.draw(overlay);
            // barra de progreso
            char tb = get_block(world, breakX, breakY);
            float mult = 1.0f;
            if (tb == (char)STONE) mult = 2.0f;
            else if (tb == (char)WOOD) mult = 0.8f;
            else if (tb == (char)LEAF) mult = 0.4f;
            else if (tb == (char)COAL) mult = 1.2f;
            else if (tb == (char)IRON) mult = 3.0f;
            else if (tb == (char)GOLD) mult = 4.0f;
            float need = BASE_BREAK_TIME * mult;
            float ratio = std::min(1.0f, breakProgress / (need + 1e-6f));
            sf::RectangleShape barBg(sf::Vector2f(TILE-6, 8));
            barBg.setPosition(breakX * TILE + 3, breakY * TILE + TILE - 12);
            barBg.setFillColor(sf::Color(0,0,0,160));
            window.draw(barBg);
            sf::RectangleShape bar(sf::Vector2f((TILE-6) * ratio, 8));
            bar.setPosition(breakX * TILE + 3, breakY * TILE + TILE - 12);
            bar.setFillColor(sf::Color::Green);
            window.draw(bar);
        }

        // draw enemies (con cámara activa) - usar texturas si están disponibles
        for (auto &e : enemies) {
            if (!e.alive) continue;
            std::vector<std::string> candidates;
            if (e.type == Enemy::ZOMBIE) candidates = {"zombie"};
            else if (e.type == Enemy::SKELETON) candidates = {"skeleton", "esqueleto"};
            else if (e.type == Enemy::SPIDER) candidates = {"spider", "araña", "arana"};
            else if (e.type == Enemy::CREEPER) candidates = {"creeper", "crepe"};

            std::string useKey;
            for (auto &c : candidates) if (textures.count(c)) { useKey = c; break; }

            if (!useKey.empty()) {
                sf::Sprite s;
                s.setTexture(textures[useKey]);
                auto &t = textures[useKey];
                if (t.getSize().x > 0 && t.getSize().y > 0) s.setScale(e.w / (float)t.getSize().x, e.h / (float)t.getSize().y);
                s.setPosition(e.x, e.y);
                // modulate sprite color by ambient
                sf::Color mod((sf::Uint8)std::min(255.0f, 255.0f * ambient), (sf::Uint8)std::min(255.0f, 255.0f * ambient), (sf::Uint8)std::min(255.0f, 255.0f * ambient));
                s.setColor(mod);
                window.draw(s);
            } else {
                sf::Color base;
                if (e.type == Enemy::ZOMBIE) base = sf::Color(50,200,50);
                else if (e.type == Enemy::SKELETON) base = sf::Color(230,230,230);
                else if (e.type == Enemy::SPIDER) base = sf::Color(20,20,20);
                else if (e.type == Enemy::CREEPER) { base = (e.fuseTimer > 0.0f) ? sf::Color(255,180,80) : sf::Color(40,200,40); }
                sf::Color col((sf::Uint8)std::min(255.0f, base.r * ambient), (sf::Uint8)std::min(255.0f, base.g * ambient), (sf::Uint8)std::min(255.0f, base.b * ambient));
                enemyShape.setFillColor(col);
                enemyShape.setPosition(e.x, e.y);
                window.draw(enemyShape);
            }
        }

        // draw player (sprite if available)
        if (playerHasTexture) {
            playerSprite.setPosition(p.px, p.py);
            sf::Color pmod((sf::Uint8)std::min(255.0f, 255.0f * ambient), (sf::Uint8)std::min(255.0f, 255.0f * ambient), (sf::Uint8)std::min(255.0f, 255.0f * ambient));
            playerSprite.setColor(pmod);
            window.draw(playerSprite);
        } else {
            sf::Color baseP = playerShape.getFillColor();
            sf::Color pcol((sf::Uint8)std::min(255.0f, baseP.r * ambient), (sf::Uint8)std::min(255.0f, baseP.g * ambient), (sf::Uint8)std::min(255.0f, baseP.b * ambient));
            playerShape.setFillColor(pcol);
            playerShape.setPosition(p.px, p.py);
            window.draw(playerShape);
            // restore base color for future frames
            playerShape.setFillColor(baseP);
        }

        // draw sword swing area (visible while active)
        if (swingActive > 0.0f) {
            float attackX = (p.fx >= 0) ? (p.px + p.w) : (p.px - SWING_RANGE);
            sf::RectangleShape atk(sf::Vector2f(SWING_RANGE, p.h));
            atk.setPosition(attackX, p.py);
            atk.setFillColor(sf::Color(255,255,255,90));
            window.draw(atk);
        }

        // draw day/night indicator (sun/moon) at top-center
        {
            float screenW = (float)VIEW_W_TILES * TILE;
            float cx = screenW * 0.5f;
            float cy = 24.0f;
            float radius = 10.0f + 6.0f * sun; // sun size varies
            sf::CircleShape orb(radius);
            // bright sun at day, pale moon at night
            sf::Color sunCol((sf::Uint8)std::min(255.0f, 255.0f * (0.9f + 0.1f * sun)), (sf::Uint8)std::min(255.0f, 200.0f * (0.6f + 0.4f * sun)), (sf::Uint8)std::min(255.0f, 120.0f * (0.4f + 0.6f * sun)));
            orb.setFillColor(sunCol);
            orb.setPosition(cx - radius, cy - radius);
            window.draw(orb);
        }

        // HUD: cambiar a vista por defecto para dibujar elementos de interfaz en pantalla
        window.setView(window.getDefaultView());
        sf::RectangleShape hudBg(sf::Vector2f((float)VIEW_W_TILES * TILE, (float)HUD_HEIGHT));
        hudBg.setPosition(0, (float)VIEW_H_TILES * TILE);
        hudBg.setFillColor(sf::Color(30,30,30,200));
        window.draw(hudBg);

        // Draw player hearts
        const float heartSize = 20.0f;
        for (int i = 0; i < MAX_HEALTH; ++i) {
            sf::RectangleShape heart(sf::Vector2f(heartSize, heartSize));
            heart.setPosition(10 + i * (heartSize + 6), 8); // hearts at top
            if (i < playerHealth) heart.setFillColor(sf::Color(220,30,30));
            else { heart.setFillColor(sf::Color(80,80,80)); heart.setOutlineThickness(2); heart.setOutlineColor(sf::Color(30,30,30)); }
            // flash when invulnerable
            if (playerInvuln > 0.0f) { sf::Color c = heart.getFillColor(); c.a = 180; heart.setFillColor(c); }
            window.draw(heart);
        }

        // tools HUD: show pickaxe/axe/shovel with keys Q/E/R below hearts
        {
            int ti = 0;
            std::vector<std::pair<std::string,char>> toolOrder = {{"pickaxe",'Q'},{"axe",'E'},{"shovel",'R'},{"sword",'T'}};
            for (auto &pr : toolOrder){
                std::string tool = pr.first; char key = pr.second;
                sf::RectangleShape tslot(sf::Vector2f(36,36));
                tslot.setPosition(10 + ti*42, 40);
                tslot.setFillColor(sf::Color(0,0,0,160));
                window.draw(tslot);
                sf::Text lab(std::string(1,key) + ":" + tool.substr(0,3), font, 14);
                lab.setPosition(14 + ti*42, 42);
                lab.setFillColor(sf::Color::White);
                window.draw(lab);
                // highlight selected tool
                if (p.selectedTool == tool){
                    sf::RectangleShape high(sf::Vector2f(40,40));
                    high.setPosition(8 + ti*42, 38);
                    high.setFillColor(sf::Color(255,255,255,40));
                    window.draw(high);
                }
                ti++;
            }
        }

        // Selected tool/block panel (top-right) - improved layout to avoid overlapping text
        {
            float screenW = (float)VIEW_W_TILES * TILE;
            float px = screenW - 280.0f;
            float py = 8.0f;
            sf::RectangleShape panel(sf::Vector2f(268.0f, 96.0f));
            panel.setPosition(px, py);
            panel.setFillColor(sf::Color(20,20,20,220));
            panel.setOutlineThickness(2); panel.setOutlineColor(sf::Color(80,80,80));
            window.draw(panel);
            // selected block big slot
            sf::RectangleShape bslot(sf::Vector2f(64,64));
            bslot.setPosition(px + 8, py + 12);
            char sb = p.selected;
            sf::Color scol = color.count(sb) ? color[sb] : sf::Color(140,140,140);
            bslot.setFillColor(scol);
            bslot.setOutlineThickness(2); bslot.setOutlineColor(sf::Color::Black);
            window.draw(bslot);
            // block name
            std::string bname = blockNames.count(sb) ? blockNames[sb] : std::string(1, sb);
            sf::Text bnameText(bname, font, 18);
            bnameText.setFillColor(sf::Color::White);
            bnameText.setPosition(px + 82, py + 16);
            window.draw(bnameText);
            // count below name
            sf::Text cnt(std::to_string(p.inv[sb]), font, 16);
            cnt.setFillColor(sf::Color::White);
            cnt.setPosition(px + 82, py + 40);
            window.draw(cnt);
            // tool area label and content (separated lines to avoid overlap)
            sf::Text tlabel("Herramienta:", font, 13);
            tlabel.setFillColor(sf::Color::White);
            tlabel.setPosition(px + 82, py + 56);
            window.draw(tlabel);
            // draw tool icon if available, else draw name on its own line
            std::string toolName = toolNames.count(p.selectedTool) ? toolNames[p.selectedTool] : (p.selectedTool.empty() ? "(none)" : p.selectedTool);
            if (!p.selectedTool.empty() && textures.count(p.selectedTool)) {
                sf::Sprite ts; ts.setTexture(textures[p.selectedTool]);
                auto &tt = textures[p.selectedTool]; if (tt.getSize().x>0 && tt.getSize().y>0) ts.setScale(48.0f / (float)tt.getSize().x, 48.0f / (float)tt.getSize().y);
                ts.setPosition(px + 188, py + 24); window.draw(ts);
                // also draw name below the label for clarity
                sf::Text tl(toolName, font, 14); tl.setFillColor(sf::Color::White); tl.setPosition(px + 82, py + 74); window.draw(tl);
            } else {
                sf::Text tl(toolName, font, 16); tl.setFillColor(sf::Color::White); tl.setPosition(px + 82, py + 72); window.draw(tl);
            }
        }

        // inventory (extendido con hojas, minerales y nuevos bloques)
        {
            std::vector<char> mapSel = {(char)GRASS,(char)DIRT,(char)STONE,(char)WOOD,(char)LEAF,(char)COAL,(char)IRON,(char)GOLD,(char)SAND,(char)SNOW,(char)NETH,(char)LAVA};
            int slots = std::min((int)mapSel.size(), INV_SLOTS);
            for (int i=0;i<slots;++i){
                char b = mapSel[i];
                sf::RectangleShape slot(sf::Vector2f(56,56));
                slot.setPosition(10 + i*66, VIEW_H_TILES * TILE + 16);
                sf::Color col = color.count(b) ? color[b] : sf::Color(100,100,100);
                slot.setFillColor(col);
                if (b==p.selected) { slot.setOutlineThickness(3); slot.setOutlineColor(sf::Color::Yellow); }
                else { slot.setOutlineThickness(1); slot.setOutlineColor(sf::Color::Black); }
                window.draw(slot);
                sf::Text t(std::to_string(p.inv[b]), font, 16);
                t.setFillColor(sf::Color::White);
                t.setPosition(10 + i*66 + 34, VIEW_H_TILES * TILE + 56);
                window.draw(t);
            }
        }

        // block picker overlay
        if (showBlockPicker) {
            // darken background
            sf::RectangleShape dark(sf::Vector2f((float)VIEW_W_TILES * TILE, (float)VIEW_H_TILES * TILE));
            dark.setFillColor(sf::Color(0,0,0,140));
            dark.setPosition(0,0);
            window.draw(dark);
            // draw centered panel with block options
            std::vector<char> picker = {(char)GRASS,(char)DIRT,(char)STONE,(char)WOOD,(char)LEAF,(char)COAL,(char)IRON,(char)GOLD,(char)SAND,(char)SNOW,(char)NETH,(char)LAVA};
            int cols = 4; int rows = (picker.size() + cols - 1) / cols;
            float slotW = 80.0f, slotH = 80.0f, gap = 12.0f;
            float panelW = cols * slotW + (cols-1)*gap;
            float panelH = rows * slotH + (rows-1)*gap;
            sf::Vector2f center((float)VIEW_W_TILES * TILE * 0.5f, (float)VIEW_H_TILES * TILE * 0.5f);
            float startX = center.x - panelW*0.5f; float startY = center.y - panelH*0.5f;
            for (int i=0;i<(int)picker.size();++i){
                int r = i / cols; int c = i % cols;
                float sx = startX + c * (slotW + gap);
                float sy = startY + r * (slotH + gap);
                sf::RectangleShape slot(sf::Vector2f(slotW, slotH));
                slot.setPosition(sx, sy);
                char b = picker[i];
                sf::Color col = color.count(b) ? color[b] : sf::Color(120,120,120);
                slot.setFillColor(col);
                slot.setOutlineThickness(2); slot.setOutlineColor(sf::Color::White);
                window.draw(slot);
                // label
                sf::Text lab(std::string(1, b), font, 20);
                lab.setFillColor(sf::Color::Black);
                lab.setPosition(sx + 8, sy + 8);
                window.draw(lab);
            }
        }

        // Help panel (toggle with H)
        if (showHelp) {
            std::vector<std::string> helpLines = {
                "Controles:",
                "A/D: mover    W/Espacio: saltar",
                "X: picar (mantener)    C/Dcho: colocar",
                "Q: Pico    E: Hacha    R: Pala    T: Espada",
                "1-0: seleccionar bloques    F: elegir bloque (overlay)",
                "K: alternar clima    H: cerrar esta ayuda"
            };
            float panelW = 560.0f;
            float lineH = 22.0f;
            float panelH = (float)helpLines.size() * lineH + 20.0f;
            float startX = ((float)VIEW_W_TILES * TILE - panelW) * 0.5f;
            float startY = ((float)VIEW_H_TILES * TILE - panelH) * 0.5f;
            sf::RectangleShape panel(sf::Vector2f(panelW, panelH));
            panel.setPosition(startX, startY);
            panel.setFillColor(sf::Color(10,10,10,220));
            panel.setOutlineThickness(2); panel.setOutlineColor(sf::Color(120,120,120));
            window.draw(panel);
            for (size_t i = 0; i < helpLines.size(); ++i) {
                sf::Text t(helpLines[i], font, 18);
                t.setFillColor(sf::Color::White);
                t.setPosition(startX + 12.0f, startY + 8.0f + i * lineH);
                window.draw(t);
            }
        }

        // FPS
        float fps = (dt > 0.0001f) ? (1.f / dt) : 0.f;
        fpsText.setString(std::to_string((int)fps) + " FPS");
        fpsText.setPosition((float)VIEW_W_TILES * TILE - 90.f, VIEW_H_TILES * TILE + 4.f);
        window.draw(fpsText);

        // (No HUD de vida ni manejo de Game Over en esta versión)

        window.display();
    }
    return 0;
}
