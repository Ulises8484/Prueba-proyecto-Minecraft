#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

// Ejemplo 2D tipo "Minecraft" usando SFML con físicas básicas solo para el jugador
// Características añadidas:
// - Física vertical: gravedad, salto, velocidad y colisión con tiles sólidos
// - Mapa más grande y una cueva/túnel subterráneo

// Map size increased: larger world while window/view remains the same
const int W = 160;
const int H = 80;
const int TILE = 32;

enum Block : char { AIR = ' ', GRASS = 'G', DIRT = 'D', STONE = 'S', WOOD = 'W', BEDR = 'B', LEAF = 'L', COAL = 'c', IRON = 'i', GOLD = 'o' };

using World = std::vector<std::string>;

struct Player {
    float px, py; // posición en píxeles
    float vx, vy; // velocidad en píxeles/s
    int fx, fy;   // dirección de mirada (-1/0/1 en x, y)
    char selected;
    std::map<char,int> inv;
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

    // rellenar suelo según heights
    for (int x = 0; x < W; ++x) {
        int g = height[x];
        for (int y = g; y < H-1; ++y) {
            if (y == g) world[y][x] = (char)GRASS;
            else if (y < g + 4) world[y][x] = (char)DIRT;
            else world[y][x] = (char)STONE;
        }
    }
    // bedrock
    for (int x = 0; x < W; ++x) world[H-1][x] = (char)BEDR;

    // árboles: probabilidad por columna, tronco vertical y copa de hojas
    for (int x = 2; x < W-2; ++x) {
        if ((std::rand() % 100) < 12) { // ~12% de probabilidad por columna
            int g = height[x];
            int trunkH = 2 + (std::rand() % 3); // 2..4
            for (int t = 1; t <= trunkH; ++t) {
                int ty = g - t;
                if (ty >= 0) world[ty][x] = (char)WOOD;
            }
            int topY = g - trunkH;
            // copa: bloque de 5x3 aproximadamente
            for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 0; ++dy) {
                int xx = x + dx; int yy = topY + dy;
                if (in_bounds(xx, yy) && world[yy][xx] == (char)AIR) world[yy][xx] = (char)LEAF;
            }
        }
    }

    // Crear cuevas/túneles y cavidades más numerosas por random walk empezando por debajo de la capa de tierra
    int tunnels = 10 + (std::rand() % 10);
    for (int i = 0; i < tunnels; ++i) {
        int tx = std::max(2, std::min(W-3, W/4 + (std::rand() % (W/2))));
        int ty = std::min(H-5, height[tx] + 4 + (std::rand() % 6));
        int len = 40 + (std::rand() % 120);
        for (int s = 0; s < len; ++s) {
            // carve a small room radius 1-2
            int radius = 1 + (std::rand() % 2);
            for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
                int xx = tx + dx; int yy = ty + dy;
                if (in_bounds(xx, yy) && yy < H-1) world[yy][xx] = (char)AIR;
            }
            // random walk biased to remain underground
            tx += (std::rand() % 3) - 1;
            ty += (std::rand() % 5) - 2; // allow more vertical variance
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

// Enemy (zombi) simple: posición, velocidad, tamaño y estado
struct Enemy {
    float x, y;
    float vx, vy;
    float w, h;
    int dir; // dirección horizontal preferida (-1 o 1)
    float moveSpeed;
    float pauseTimer; // tiempo de pausa para comportamiento torpe
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
    p.inv[(char)GRASS]=10; p.inv[(char)DIRT]=8; p.inv[(char)STONE]=6; p.inv[(char)WOOD]=3; p.inv[(char)BEDR]=0;
    p.inv[(char)LEAF]=0; p.inv[(char)COAL]=0; p.inv[(char)IRON]=0; p.inv[(char)GOLD]=0;

    // Ventana ajustada a 1280x720: calculamos tiles visibles y usamos una cámara que sigue al jugador
    const int VIEW_W_TILES = 40; // 1280 / 32
    const int VIEW_H_TILES = 20; // (720 - HUD) / 32
    const int HUD_HEIGHT = 80;
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
        {(char)DIRT, sf::Color(134, 96, 67)},
        {(char)STONE, sf::Color(120,120,120)},
        {(char)WOOD, sf::Color(150, 111, 51)},
        {(char)BEDR, sf::Color(40,40,40)},
        {(char)LEAF, sf::Color(110,180,80)},
        {(char)COAL, sf::Color(30,30,30)},
        {(char)IRON, sf::Color(180,180,200)},
        {(char)GOLD, sf::Color(212,175,55)}
    };

    sf::Font font;
    font.loadFromFile("assets/fonts/Minecraft.ttf");

    // Música de fondo (C418) — si existe en assets/music
    sf::Music bgm;
    if (bgm.openFromFile("assets/music/C418-Subwoofer-Lullaby-Minecraft-Volume-Alpha.ogg")) {
        bgm.setLoop(true);
        bgm.setVolume(40);
        bgm.play();
    } else {
        std::cerr << "Aviso: no se pudo abrir 'assets/music/C418-Subwoofer-Lullaby-Minecraft-Volume-Alpha.ogg'. Comprueba que el archivo y la carpeta 'assets/music' existen." << std::endl;
    }

    sf::RectangleShape tileShape(sf::Vector2f(TILE, TILE));
    sf::RectangleShape playerShape(sf::Vector2f(p.w, p.h));
    playerShape.setFillColor(sf::Color::Yellow);

    // FPS display
    sf::Text fpsText;
    fpsText.setFont(font);
    fpsText.setCharacterSize(14);
    fpsText.setFillColor(sf::Color::White);

    // Crear un enemigo zombi simple (cuadrado verde, lento y torpe)
    Enemy zombie{};
    zombie.w = p.w; zombie.h = p.h;
    int spawnEx = std::min(W-2, W/2 + 6);
    int spawnTileEy = 0;
    for (int y = 0; y < H; ++y) {
        if (get_block(world, spawnEx, y) != (char)AIR) { spawnTileEy = y - 1; break; }
    }
    if (spawnTileEy < 0) spawnTileEy = H - 6;
    zombie.x = spawnEx * TILE; zombie.y = spawnTileEy * TILE;
    zombie.vx = 0; zombie.vy = 0; zombie.dir = -1; zombie.moveSpeed = 60.0f; zombie.pauseTimer = 0.0f;

    sf::RectangleShape enemyShape(sf::Vector2f(zombie.w, zombie.h));
    enemyShape.setFillColor(sf::Color(50,200,50)); // verde zombi

    const float GRAVITY = 1500.0f; // px/s^2
    const float MOVE_SPEED = 150.0f; // px/s
    const float JUMP_SPEED = 520.0f; // px/s

    sf::Clock clock;
    // Picar bloques por tiempo
    bool breaking = false;
    int breakX = -1, breakY = -1;
    float breakProgress = 0.0f;
    const float BASE_BREAK_TIME = 0.8f; // segundos para bloques normales
    while (window.isOpen()){
        sf::Event ev;
        while (window.pollEvent(ev)){
            if (ev.type == sf::Event::Closed) window.close();
            if (ev.type == sf::Event::KeyPressed){
                if (ev.key.code == sf::Keyboard::Escape) window.close();
                if (ev.key.code == sf::Keyboard::Num1) p.selected=(char)GRASS;
                if (ev.key.code == sf::Keyboard::Num2) p.selected=(char)DIRT;
                if (ev.key.code == sf::Keyboard::Num3) p.selected=(char)STONE;
                if (ev.key.code == sf::Keyboard::Num4) p.selected=(char)WOOD;
                if (ev.key.code == sf::Keyboard::Num5) p.selected=(char)LEAF;
                if (ev.key.code == sf::Keyboard::Num6) p.selected=(char)COAL;
                if (ev.key.code == sf::Keyboard::Num7) p.selected=(char)IRON;
                if (ev.key.code == sf::Keyboard::Num8) p.selected=(char)GOLD;
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
            }
            if (ev.type == sf::Event::MouseButtonPressed){
                // click handling: colocar con botón derecho (inmediato). Picar con botón izquierdo ahora se maneja manteniendo pulsado (ver loop principal).
                sf::Vector2i m = sf::Mouse::getPosition(window);
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

        // --- Mecánica de picar por tiempo ---
        bool keyBreak = sf::Keyboard::isKeyPressed(sf::Keyboard::X);
        bool mouseBreak = sf::Mouse::isButtonPressed(sf::Mouse::Left);
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

        // Actualizar enemigo (zombi) - física simple y AI torpe
        zombie.vy += GRAVITY * dt;
        if (zombie.vy > 2000.0f) zombie.vy = 2000.0f;
        // comportamiento: si el jugador está cerca, avanza hacia él lentamente; si no, deambula
        float exCenter = zombie.x + zombie.w*0.5f;
        float pxCenter = p.px + p.w*0.5f;
        float dxE = pxCenter - exCenter;
        float distE = std::abs(dxE);
        if (zombie.pauseTimer > 0.0f) {
            zombie.pauseTimer -= dt; zombie.vx = 0.0f;
        } else {
            if (distE < 500.0f) {
                zombie.vx = (dxE > 0.0f) ? zombie.moveSpeed : -zombie.moveSpeed;
            } else {
                zombie.vx = zombie.moveSpeed * zombie.dir;
                if ((std::rand() % 1000) < 8) { zombie.dir = -zombie.dir; zombie.pauseTimer = 0.35f; zombie.vx = 0.0f; }
            }
        }
        float newEx = zombie.x + zombie.vx * dt;
        resolveHorizontalEnemy(world, zombie, newEx);
        float newEy = zombie.y + zombie.vy * dt;
        resolveVerticalEnemy(world, zombie, newEy);

        window.clear(sf::Color(135,206,235));

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

        // dibujamos el mundo usando la cámara
        window.setView(camera);
        for (int y=0;y<H;++y){
            for (int x=0;x<W;++x){
                char b = get_block(world,x,y);
                sf::Color col = color.count(b) ? color[b] : sf::Color::Magenta;
                tileShape.setPosition(x*TILE, y*TILE);
                tileShape.setFillColor(col);
                window.draw(tileShape);
            }
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

        // draw enemy (con cámara activa)
        enemyShape.setPosition(zombie.x, zombie.y);
        window.draw(enemyShape);

        // draw player
        playerShape.setPosition(p.px, p.py);
        window.draw(playerShape);

        // HUD: cambiar a vista por defecto para dibujar elementos de interfaz en pantalla
        window.setView(window.getDefaultView());
        sf::RectangleShape hudBg(sf::Vector2f((float)VIEW_W_TILES * TILE, (float)HUD_HEIGHT));
        hudBg.setPosition(0, (float)VIEW_H_TILES * TILE);
        hudBg.setFillColor(sf::Color(30,30,30,200));
        window.draw(hudBg);

        // inventory (extendido con hojas y minerales)
        for (int i=0;i<8;++i){
            char mapSel[8] = {(char)GRASS,(char)DIRT,(char)STONE,(char)WOOD,(char)LEAF,(char)COAL,(char)IRON,(char)GOLD};
            char b = mapSel[i];
            sf::RectangleShape slot(sf::Vector2f(48,48));
            slot.setPosition(10 + i*60, VIEW_H_TILES * TILE + 16);
            slot.setFillColor(color[b]);
            if (b==p.selected) { slot.setOutlineThickness(3); slot.setOutlineColor(sf::Color::Yellow); }
            else { slot.setOutlineThickness(1); slot.setOutlineColor(sf::Color::Black); }
            window.draw(slot);
            sf::Text t(std::to_string(p.inv[b]), font, 16);
            t.setFillColor(sf::Color::White);
            t.setPosition(10 + i*60 + 28, VIEW_H_TILES * TILE + 48);
            window.draw(t);
        }

        sf::Text instr("W/Space: saltar  A/D: mover  X: picar  C: colocar  1-8: seleccionar", font, 14);
        instr.setFillColor(sf::Color::White);
        instr.setPosition(10, VIEW_H_TILES * TILE + 4);
        window.draw(instr);

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
