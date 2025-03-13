#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <iostream>
#include <vector>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <limits>
#include <windows.h>
#include <algorithm>
#include <string>
#include <chrono>
#include <random>
#include <memory>
#include <thread>

using namespace std;

// Constants for better performance
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const int MAP_WIDTH = 24;
const int MAP_HEIGHT = 24;
const int CELL_SIZE = 64;  // Size of each map cell

// Reduce raycasting resolution for better performance
const int RAY_WIDTH = SCREEN_WIDTH / 4;  // Even fewer rays for better performance
const float RAY_SCALE = static_cast<float>(SCREEN_WIDTH) / RAY_WIDTH;

// Add at the top of your file
const int ANGLE_TABLE_SIZE = 1024;
float sinTable[ANGLE_TABLE_SIZE];
float cosTable[ANGLE_TABLE_SIZE];

// Basic 2D vector for map calculations
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}
    
    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * y); }
    float length() const { return sqrt(x * x + y * y); }
    Vec2 normalize() const { 
        float len = length(); 
        return len > 0.0001f ? Vec2(x / len, y / len) : Vec2(0, 0); 
    }
};

// Function declarations for fast trigonometric calculations
float fastSin(float angle);
float fastCos(float angle);

// Function implementations
float fastSin(float angle) {
    int index = int(angle * ANGLE_TABLE_SIZE / (2.0f * M_PI)) % ANGLE_TABLE_SIZE;
    if (index < 0) index += ANGLE_TABLE_SIZE;
    return sinTable[index];
}

float fastCos(float angle) {
    int index = int(angle * ANGLE_TABLE_SIZE / (2.0f * M_PI)) % ANGLE_TABLE_SIZE;
    if (index < 0) index += ANGLE_TABLE_SIZE;
    return cosTable[index];
}

// Player class
class Player {
public:
    Vec2 position;
    Vec2 direction;
    Vec2 plane;  // Camera plane
    float moveSpeed;
    float rotSpeed;
    int health;
    bool hasWeapon;
    
    Player() : position(5, 5), direction(-1, 0), plane(0, 0.66f), 
              moveSpeed(0.1f), rotSpeed(0.05f), health(100), hasWeapon(true) {}
    
    void move(float forward, float strafe) {
        // Move forward/backward
        Vec2 moveVec = direction * (forward * moveSpeed);
        position = position + moveVec;
        
        // Strafe left/right
        Vec2 strafeVec = Vec2(-direction.y, direction.x) * (strafe * moveSpeed);
        position = position + strafeVec;
    }
    
    void rotate(float angle) {
        // Rotate direction and plane vectors - precompute sin/cos for performance
        float cosAngle = fastCos(angle);
        float sinAngle = fastSin(angle);
        
        float oldDirX = direction.x;
        direction.x = direction.x * cosAngle - direction.y * sinAngle;
        direction.y = oldDirX * sinAngle + direction.y * cosAngle;
        
        float oldPlaneX = plane.x;
        plane.x = plane.x * cosAngle - plane.y * sinAngle;
        plane.y = oldPlaneX * sinAngle + plane.y * cosAngle;
    }
};

// Simple enemy
class Enemy {
public:
    Vec2 position;
    float speed;
    int health;
    bool isDead;
    
    Enemy(float x, float y) : position(x, y), speed(0.03f), health(50), isDead(false) {}
    
    void update(const Player& player, const int worldMap[MAP_WIDTH][MAP_HEIGHT]) {
        if (isDead) return;
        
        // Simple AI: move toward player if there's a clear path
        Vec2 toPlayer = Vec2(player.position.x - position.x, player.position.y - position.y);
        float distance = toPlayer.length();
        
        if (distance > 0.5f) {
            Vec2 moveDir = toPlayer.normalize();
            Vec2 newPos = Vec2(position.x + moveDir.x * speed, position.y + moveDir.y * speed);
            
            // Check for wall collision
            if (worldMap[int(newPos.x)][int(position.y)] == 0) {
                position.x = newPos.x;
            }
            if (worldMap[int(position.x)][int(newPos.y)] == 0) {
                position.y = newPos.y;
            }
        }
    }
};

// Game class
class Game {
private:
    Player player;
    vector<Enemy> enemies;
    int worldMap[MAP_WIDTH][MAP_HEIGHT];
    POINT lastMousePos;
    bool mouseCaptured;
    bool gameOver;
    HBITMAP backBuffer;
    BITMAPINFO bmpInfo;
    void* backBufferPixels;
    unsigned int textureWall[CELL_SIZE * CELL_SIZE];
    unsigned int textureFloor[CELL_SIZE * CELL_SIZE];
    unsigned int textureEnemy[CELL_SIZE * CELL_SIZE];
    unsigned int* renderBuffer; // Pre-allocated buffer for rendering
    float* zBuffer; // Depth buffer for sprites
    HDC memDC; // Create a single compatible DC at initialization rather than per frame
    
public:
    Game() : mouseCaptured(false), gameOver(false), backBuffer(NULL), backBufferPixels(NULL),
             renderBuffer(NULL), zBuffer(NULL), memDC(NULL) {
        // Initialize buffers for rendering optimization
        renderBuffer = new unsigned int[SCREEN_WIDTH * SCREEN_HEIGHT];
        zBuffer = new float[SCREEN_WIDTH];

        // Initialize the world map (1 = wall, 0 = empty)
        for (int x = 0; x < MAP_WIDTH; x++) {
            for (int y = 0; y < MAP_HEIGHT; y++) {
                if (x == 0 || y == 0 || x == MAP_WIDTH - 1 || y == MAP_HEIGHT - 1) {
                    worldMap[x][y] = 1;  // Border walls
                } else {
                    worldMap[x][y] = 0;  // Empty space
                }
            }
        }
        
        // Add some interior walls to make a maze-like structure
        srand(static_cast<unsigned>(time(nullptr))); // Initialize random seed
        for (int i = 0; i < 50; i++) {
            int x = rand() % (MAP_WIDTH - 2) + 1;
            int y = rand() % (MAP_HEIGHT - 2) + 1;
            // Don't place walls near the player spawn point
            if (abs(x - player.position.x) > 3 || abs(y - player.position.y) > 3) {
                worldMap[x][y] = 1;
            }
        }
        
        // Add some enemies
        for (int i = 0; i < 5; i++) {
            float x = rand() % (MAP_WIDTH - 4) + 2;
            float y = rand() % (MAP_HEIGHT - 4) + 2;
            // Don't spawn enemies too close to the player
            if (abs(x - player.position.x) > 5 || abs(y - player.position.y) > 5) {
                enemies.push_back(Enemy(x, y));
            }
        }
        
        // Initialize textures with simple patterns
        createTextures();
        
        // Set up bitmap info
        ZeroMemory(&bmpInfo, sizeof(BITMAPINFO));
        bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmpInfo.bmiHeader.biWidth = SCREEN_WIDTH;
        bmpInfo.bmiHeader.biHeight = -SCREEN_HEIGHT;  // Negative for top-down
        bmpInfo.bmiHeader.biPlanes = 1;
        bmpInfo.bmiHeader.biBitCount = 32;
        bmpInfo.bmiHeader.biCompression = BI_RGB;

        // Initialize trigonometric tables
        initTrigTables();
    }
    
    ~Game() {
        if (backBuffer) DeleteObject(backBuffer);
        if (memDC) DeleteDC(memDC);
        if (renderBuffer) delete[] renderBuffer;
        if (zBuffer) delete[] zBuffer;
    }
    
    void createTextures() {
        // Simple checkerboard pattern for walls
        for (int x = 0; x < CELL_SIZE; x++) {
            for (int y = 0; y < CELL_SIZE; y++) {
                int pattern = (x / 8 + y / 8) % 2;
                textureWall[y * CELL_SIZE + x] = pattern ? 0xFF0000FF : 0xFF888888;
            }
        }
        
        // Floor texture
        for (int x = 0; x < CELL_SIZE; x++) {
            for (int y = 0; y < CELL_SIZE; y++) {
                int pattern = (x / 16 + y / 16) % 2;
                textureFloor[y * CELL_SIZE + x] = pattern ? 0xFF005500 : 0xFF003300;
            }
        }
        
        // Enemy texture (simple red blob)
        for (int x = 0; x < CELL_SIZE; x++) {
            for (int y = 0; y < CELL_SIZE; y++) {
                float dx = x - CELL_SIZE/2;
                float dy = y - CELL_SIZE/2;
                float dist = sqrt(dx*dx + dy*dy);
                if (dist < CELL_SIZE/3) {
                    textureEnemy[y * CELL_SIZE + x] = 0xFFFF0000;
                } else if (dist < CELL_SIZE/2) {
                    textureEnemy[y * CELL_SIZE + x] = 0x88FF0000;
                } else {
                    textureEnemy[y * CELL_SIZE + x] = 0;
                }
            }
        }
    }
    
    bool init(HDC hdc) {
        // Create back buffer for double buffering
        memDC = CreateCompatibleDC(hdc);
        backBuffer = CreateCompatibleBitmap(hdc, SCREEN_WIDTH, SCREEN_HEIGHT);
        if (!backBuffer || !memDC) return false;
        SelectObject(memDC, backBuffer);
        return true;
    }
    
    void setMouseCaptured(bool captured) {
        mouseCaptured = captured;
    }
    
    bool isMouseCaptured() const {
        return mouseCaptured;
    }
    
    POINT& getLastMousePos() {
        return lastMousePos;
    }
    
    void update() {
        if (gameOver) return;
        
        // Handle keyboard input for movement
        if (GetAsyncKeyState('W') & 0x8000) {
            movePlayer(1.0f, 0.0f);
        }
        if (GetAsyncKeyState('S') & 0x8000) {
            movePlayer(-1.0f, 0.0f);
        }
        if (GetAsyncKeyState('A') & 0x8000) {
            movePlayer(0.0f, -1.0f);
        }
        if (GetAsyncKeyState('D') & 0x8000) {
            movePlayer(0.0f, 1.0f);
        }
        
        // Handle mouse for rotation
        if (mouseCaptured) {
            POINT currentMousePos;
            GetCursorPos(&currentMousePos);
            
            float dx = static_cast<float>(currentMousePos.x - lastMousePos.x);
            player.rotate(-dx * 0.01f);
            
            // Reset cursor to center
            SetCursorPos(lastMousePos.x, lastMousePos.y);
        }
        
        // Update enemies - only every other frame for performance
        static int frameCount = 0;
        if (++frameCount % 2 == 0) {
            for (auto& enemy : enemies) {
                enemy.update(player, worldMap);
                
                // Check collision with player
                float dist = (Vec2(player.position.x - enemy.position.x, 
                                player.position.y - enemy.position.y)).length();
                if (dist < 0.5f && !enemy.isDead) {
                    player.health -= 1;  // Enemy deals damage when close
                }
            }
        }
        
        // Check for player shooting
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && player.hasWeapon) {
            shootWeapon();
        }
        
        // Check game over condition
        if (player.health <= 0) {
            gameOver = true;
        }
    }
    
    // FIX 1: Correct the strafe movement direction in movePlayer method
    void movePlayer(float forward, float strafe) {
        Vec2 newPos = player.position;
        
        // Calculate new position
        if (forward != 0) {
            newPos.x += player.direction.x * forward * player.moveSpeed;
            newPos.y += player.direction.y * forward * player.moveSpeed;
        }
        
        if (strafe != 0) {
            // Fix the A/D controls by reversing the sign on strafe
            newPos.x += player.direction.y * strafe * player.moveSpeed;  // Changed sign
            newPos.y += -player.direction.x * strafe * player.moveSpeed; // Changed sign
        }
        
        // Add a small buffer to collision detection to keep the player slightly away from walls
        float wallBuffer = 0.1f;

        if (worldMap[int(newPos.x + (forward > 0 ? wallBuffer : -wallBuffer))][int(player.position.y)] == 0) {
            player.position.x = newPos.x;
        }
        if (worldMap[int(player.position.x)][int(newPos.y + (forward > 0 ? wallBuffer : -wallBuffer))] == 0) {
            player.position.y = newPos.y;
        }
    }
    
    void shootWeapon() {
        // Simple shooting - check if any enemy is in front of player
        for (auto& enemy : enemies) {
            if (enemy.isDead) continue;
            
            // Calculate angle to enemy relative to player's direction
            Vec2 toEnemy = Vec2(enemy.position.x - player.position.x, 
                              enemy.position.y - player.position.y);
            float enemyDist = toEnemy.length();
            
            // Normalize to get direction
            toEnemy = Vec2(toEnemy.x / enemyDist, toEnemy.y / enemyDist);
            
            // Calculate dot product to find angle
            float dotProduct = player.direction.x * toEnemy.x + player.direction.y * toEnemy.y;
            float angle = acos(dotProduct);
            
            // If enemy is within shooting arc (about 15 degrees) and not too far
            if (angle < 0.26f && enemyDist < 8.0f) {
                enemy.health -= 10;
                if (enemy.health <= 0) {
                    enemy.isDead = true;
                }
                break;  // Only hit first enemy in line
            }
        }
    }
    
    // FIX 2: Add minimum distance check in renderScene method
    void renderScene() {
        // Clear Z-buffer
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            zBuffer[x] = std::numeric_limits<float>::max();
        }

        // Perform raycasting for walls at reduced resolution
        for (int x = 0; x < RAY_WIDTH; x++) {
            // Calculate ray position and direction
            float cameraX = 2.0f * x / RAY_WIDTH - 1.0f; // X-coordinate in camera space
            Vec2 rayDir = Vec2(
                player.direction.x + player.plane.x * cameraX,
                player.direction.y + player.plane.y * cameraX
            );
            
            // Current map position
            int mapX = int(player.position.x);
            int mapY = int(player.position.y);
            
            // Length of ray from one side to next in map
            float deltaDistX = (rayDir.x == 0) ? 1e30f : abs(1.0f / rayDir.x);
            float deltaDistY = (rayDir.y == 0) ? 1e30f : abs(1.0f / rayDir.y);
            
            // Length of ray from current position to next x or y-side
            float sideDistX, sideDistY;
            
            // Direction to step in
            int stepX, stepY;
            
            // Calculate step and initial sideDist
            if (rayDir.x < 0) {
                stepX = -1;
                sideDistX = (player.position.x - mapX) * deltaDistX;
            } else {
                stepX = 1;
                sideDistX = (mapX + 1.0f - player.position.x) * deltaDistX;
            }
            if (rayDir.y < 0) {
                stepY = -1;
                sideDistY = (player.position.y - mapY) * deltaDistY;
            } else {
                stepY = 1;
                sideDistY = (mapY + 1.0f - player.position.y) * deltaDistY;
            }
            
            // DDA algorithm
            int hit = 0;  // Wall hit?
            int side;     // NS or EW wall hit?
            
            while (hit == 0) {
                // Jump to next map square
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }
                
                // Check if ray hit a wall
                if (mapX >= 0 && mapY >= 0 && mapX < MAP_WIDTH && mapY < MAP_HEIGHT && worldMap[mapX][mapY] > 0) {
                    hit = 1;
                }
            }
            
            // Calculate distance to the wall
            float perpWallDist;
            if (side == 0) {
                perpWallDist = sideDistX - deltaDistX;
            } else {
                perpWallDist = sideDistY - deltaDistY;
            }
            
            // Add minimum distance check to prevent wall wiggling
            perpWallDist = max(perpWallDist, 0.05f);
            
            // Calculate height of wall slice to draw
            int lineHeight = int(SCREEN_HEIGHT / perpWallDist);
            
            // Cap maximum wall height to prevent extreme distortion
            lineHeight = min(lineHeight, SCREEN_HEIGHT * 10);
            
            // Calculate lowest and highest pixel to draw
            int drawStart = -lineHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawStart < 0) drawStart = 0;
            int drawEnd = lineHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawEnd >= SCREEN_HEIGHT) drawEnd = SCREEN_HEIGHT - 1;
            
            // Texture calculations
            float wallX;
            if (side == 0) {
                wallX = player.position.y + perpWallDist * rayDir.y;
            } else {
                wallX = player.position.x + perpWallDist * rayDir.x;
            }
            wallX -= floor(wallX);
            
            // X coordinate in the texture
            int texX = int(wallX * CELL_SIZE);
            if (side == 0 && rayDir.x > 0) texX = CELL_SIZE - texX - 1;
            if (side == 1 && rayDir.y < 0) texX = CELL_SIZE - texX - 1;
            
            // Draw the wall slice for each scaled ray
            for (int screenX = x * RAY_SCALE; screenX < (x + 1) * RAY_SCALE; screenX++) {
                // Make sure we don't go out of bounds
                if (screenX >= SCREEN_WIDTH) break;
                
                // Store depth information for sprite rendering
                zBuffer[screenX] = perpWallDist;
                
                // Draw the wall slice
                for (int y = drawStart; y < drawEnd; y++) {
                    int texY = int((float)(y - drawStart) / lineHeight * CELL_SIZE);
                    unsigned int texel = textureWall[texY * CELL_SIZE + texX];
                    
                    // Darken one side for 3D effect
                    if (side == 1) {
                        unsigned int r = (texel >> 16) & 0xFF;
                        unsigned int g = (texel >> 8) & 0xFF;
                        unsigned int b = texel & 0xFF;
                        r = r * 0.7;
                        g = g * 0.7;
                        b = b * 0.7;
                        texel = (0xFF << 24) | (r << 16) | (g << 8) | b;
                    }
                    
                    renderBuffer[y * SCREEN_WIDTH + screenX] = texel;
                }
                
                // Draw floor and ceiling - simplified for performance
                unsigned int ceilingColor = 0xFF333333;  // Ceiling color
                unsigned int floorColor = 0xFF444444;    // Floor color
                
                for (int y = 0; y < drawStart; y++) {
                    renderBuffer[y * SCREEN_WIDTH + screenX] = ceilingColor;
                }
                for (int y = drawEnd + 1; y < SCREEN_HEIGHT; y++) {
                    renderBuffer[y * SCREEN_WIDTH + screenX] = floorColor;
                }
            }
        }
        
        // Render sprites (enemies)
        renderSprites();
    }
    
    void renderSprites() {
        // Only process visible enemies
        vector<pair<float, int>> spriteOrder;
        
        for (size_t i = 0; i < enemies.size(); i++) {
            // Skip processing for enemies that are far away
            float dx = enemies[i].position.x - player.position.x;
            float dy = enemies[i].position.y - player.position.y;
            float dist = dx*dx + dy*dy;
            
            if (dist > 400.0f || enemies[i].isDead) continue; // Skip distant enemies
            
            spriteOrder.push_back(make_pair(dist, i));
        }
        
        // Sort enemies by distance (for correct transparency)
        sort(spriteOrder.begin(), spriteOrder.end(), 
             [](const pair<float, int>& a, const pair<float, int>& b) {
                 return a.first > b.first;  // Sort from far to near
             });
        
        // Draw sprites from furthest to nearest
        for (auto& pair : spriteOrder) {
            int i = pair.second;
            Enemy& enemy = enemies[i];
            
            // Don't draw dead enemies
            if (enemy.isDead) continue;
            
            // Calculate sprite position relative to player
            float spriteX = enemy.position.x - player.position.x;
            float spriteY = enemy.position.y - player.position.y;
            
            // Transform sprite with the inverse camera matrix
            float invDet = 1.0f / (player.plane.x * player.direction.y - player.direction.x * player.plane.y);
            float transformX = invDet * (player.direction.y * spriteX - player.direction.x * spriteY);
            float transformY = invDet * (-player.plane.y * spriteX + player.plane.x * spriteY);
            
            // Sprite is behind the camera
            if (transformY <= 0.1f) continue;
            
            // Calculate sprite screen position
            int spriteScreenX = int((SCREEN_WIDTH / 2) * (1 + transformX / transformY));
            
            // Calculate sprite height and width
            int spriteHeight = abs(int(SCREEN_HEIGHT / transformY));
            int spriteWidth = abs(int(SCREEN_HEIGHT / transformY));
            
            // Scale down large sprites for performance
            if (spriteHeight > SCREEN_HEIGHT * 2) spriteHeight = SCREEN_HEIGHT * 2;
            if (spriteWidth > SCREEN_WIDTH * 2) spriteWidth = SCREEN_WIDTH * 2;
            
            // Calculate drawing bounds
            int drawStartY = -spriteHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawStartY < 0) drawStartY = 0;
            int drawEndY = spriteHeight / 2 + SCREEN_HEIGHT / 2;
            if (drawEndY >= SCREEN_HEIGHT) drawEndY = SCREEN_HEIGHT - 1;
            
            int drawStartX = -spriteWidth / 2 + spriteScreenX;
            if (drawStartX < 0) drawStartX = 0;
            int drawEndX = spriteWidth / 2 + spriteScreenX;
            if (drawEndX >= SCREEN_WIDTH) drawEndX = SCREEN_WIDTH - 1;
            
            // Skip drawing sprites that are off-screen
            if (drawEndX < 0 || drawStartX >= SCREEN_WIDTH) continue;
            
            // Optimization: Increase stepping to draw fewer pixels of the sprite
            int step = 1;
            if (spriteHeight > SCREEN_HEIGHT / 2) step = 2; // Use larger steps for large sprites
            
            // Loop through every pixel of the sprite (with optimization step)
            for (int x = drawStartX; x < drawEndX; x += step) {
                // Bounds check
                if (x < 0 || x >= SCREEN_WIDTH) continue;
                
                // Check if sprite is behind a wall
                if (transformY > zBuffer[x]) continue;
                
                int texX = int((x - (-spriteWidth / 2 + spriteScreenX)) * CELL_SIZE / spriteWidth);
                
                for (int y = drawStartY; y < drawEndY; y += step) {
                    if (y < 0 || y >= SCREEN_HEIGHT) continue;
                    
                    int texY = int((y - drawStartY) * CELL_SIZE / spriteHeight);
                    unsigned int texel = textureEnemy[texY * CELL_SIZE + texX];
                    
                    // Only draw non-transparent pixels
                    if ((texel & 0xFF000000) != 0) {
                        renderBuffer[y * SCREEN_WIDTH + x] = texel;
                        // Fill gaps if step > 1 to avoid a checkerboard effect
                        if (step > 1) {
                            if (x + 1 < drawEndX && x + 1 < SCREEN_WIDTH) 
                                renderBuffer[y * SCREEN_WIDTH + x + 1] = texel;
                            if (y + 1 < drawEndY && y + 1 < SCREEN_HEIGHT) 
                                renderBuffer[(y + 1) * SCREEN_WIDTH + x] = texel;
                            if (x + 1 < drawEndX && y + 1 < drawEndY && x + 1 < SCREEN_WIDTH && y + 1 < SCREEN_HEIGHT) 
                                renderBuffer[(y + 1) * SCREEN_WIDTH + x + 1] = texel;
                        }
                    }
                }
            }
        }
    }
    
    void renderHUD() {
        // Draw health bar
        int healthBarWidth = 200;
        int healthBarHeight = 20;
        int healthBarX = 20;
        int healthBarY = SCREEN_HEIGHT - 40;
        
        // Health bar background
        for (int y = healthBarY; y < healthBarY + healthBarHeight; y++) {
            for (int x = healthBarX; x < healthBarX + healthBarWidth; x++) {
                renderBuffer[y * SCREEN_WIDTH + x] = 0xFF222222;
            }
        }
        
        // Health bar fill
        int fillWidth = (player.health * healthBarWidth) / 100;
        for (int y = healthBarY; y < healthBarY + healthBarHeight; y++) {
            for (int x = healthBarX; x < healthBarX + fillWidth; x++) {
                renderBuffer[y * SCREEN_WIDTH + x] = 0xFF00FF00;
            }
        }
        
        // Weapon crosshair
        if (player.hasWeapon) {
            int crosshairSize = 10;
            int centerX = SCREEN_WIDTH / 2;
            int centerY = SCREEN_HEIGHT / 2;
            
            for (int x = centerX - crosshairSize; x <= centerX + crosshairSize; x++) {
                if (x >= 0 && x < SCREEN_WIDTH) {
                    renderBuffer[centerY * SCREEN_WIDTH + x] = 0xFFFFFFFF;
                }
            }
            for (int y = centerY - crosshairSize; y <= centerY + crosshairSize; y++) {
                if (y >= 0 && y < SCREEN_HEIGHT) {
                    renderBuffer[y * SCREEN_WIDTH + centerX] = 0xFFFFFFFF;
                }
            }
        }
        
        // Draw game over text if needed
        if (gameOver) {
            const char* text = "GAME OVER";
            int textWidth = 9 * 20;  // Approximate width
            int textX = (SCREEN_WIDTH - textWidth) / 2;
            int textY = SCREEN_HEIGHT / 2;
            
            for (int y = textY; y < textY + 40; y++) {
                for (int x = textX; x < textX + textWidth; x++) {
                    renderBuffer[y * SCREEN_WIDTH + x] = 0xFFFF0000;
                }
            }
        }
    }

    // Add this method to your Game class, just before or after the renderHUD method:

    void render(HDC hdc) {
        // First render the 3D scene (walls, floor, ceiling)
        renderScene();
        
        // Then render sprites (enemies)
        // Note: renderSprites is already called from renderScene()
        
        // Finally render the HUD on top
        renderHUD();
        
        // Blit the buffer to the screen
        SetDIBitsToDevice(
            hdc,                        // Destination HDC
            0, 0,                       // Destination x, y
            SCREEN_WIDTH, SCREEN_HEIGHT, // Width, Height
            0, 0,                       // Source x, y
            0,                          // First scan line
            SCREEN_HEIGHT,              // Number of scan lines
            renderBuffer,               // Array of RGB values
            &bmpInfo,                   // DIB information
            DIB_RGB_COLORS              // RGB values
        );
    }

    // Initialize in constructor
    void initTrigTables() {
        for (int i = 0; i < ANGLE_TABLE_SIZE; i++) {
            float angle = 2.0f * M_PI * i / ANGLE_TABLE_SIZE;
            sinTable[i] = sin(angle);
            cosTable[i] = cos(angle);
        }
    }
};

// Windows procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Game* game = (Game*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {
        case WM_CREATE: {
            // Create game instance
            Game* newGame = new Game();
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)newGame);
            
            // Initialize the game
            HDC hdc = GetDC(hwnd);
            newGame->init(hdc);
            ReleaseDC(hwnd, hdc);
            return 0;
        }
        
        case WM_DESTROY:
            if (game) {
                delete game;
            }
            PostQuitMessage(0);
            return 0;
            
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                PostQuitMessage(0);
            }
            return 0;
            
        case WM_LBUTTONDOWN:
            if (game) {
                if (!game->isMouseCaptured()) {
                    game->setMouseCaptured(true);
                    GetCursorPos(&game->getLastMousePos());
                    ShowCursor(FALSE);
                    SetCapture(hwnd);
                }
            }
            return 0;
            
        case WM_RBUTTONDOWN:
            if (game) {
                game->setMouseCaptured(false);
                ShowCursor(TRUE);
                ReleaseCapture();
            }
            return 0;
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            if (game) {
                game->render(hdc);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
    // Register window class
    const wchar_t CLASS_NAME[] = L"DoomStyleGameClass";
    
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClass(&wc);
    
    // Create window
    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"DOOM-style Game",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, SCREEN_WIDTH, SCREEN_HEIGHT,
        NULL, NULL, hInstance, NULL
    );
    
    if (hwnd == NULL) {
        return 0;
    }
    
    // Adjust window size to account for borders
    RECT clientRect, windowRect;
    GetClientRect(hwnd, &clientRect);
    GetWindowRect(hwnd, &windowRect);
    
    int borderWidth = (windowRect.right - windowRect.left) - clientRect.right;
    int borderHeight = (windowRect.bottom - windowRect.top) - clientRect.bottom;
    
    SetWindowPos(hwnd, NULL, 0, 0, SCREEN_WIDTH + borderWidth, SCREEN_HEIGHT + borderHeight, 
                 SWP_NOMOVE | SWP_NOZORDER);
    
    ShowWindow(hwnd, nCmdShow);
    
    // Initialize trigonometric tables
    for (int i = 0; i < ANGLE_TABLE_SIZE; i++) {
        float angle = 2.0f * M_PI * i / ANGLE_TABLE_SIZE;
        sinTable[i] = sin(angle);
        cosTable[i] = cos(angle);
    }
    
    // Get the game instance from window
    Game* game = (Game*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    // Game loop
    MSG msg = {};
    DWORD lastTime = GetTickCount();
    
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            
            if (msg.message == WM_QUIT) {
                return (int)msg.wParam;
            }
        }
        
        // Ensure we have a valid game instance
        game = (Game*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (!game) {
            Sleep(10);
            continue;
        }
        
        // Ensure stable frame rate
        DWORD currentTime = GetTickCount();
        DWORD deltaTime = currentTime - lastTime;
        
        if (deltaTime >= 16) {  // Cap at roughly 60 FPS
            // Update game
            game->update();
            
            // Render
            HDC hdc = GetDC(hwnd);
            game->render(hdc);
            ReleaseDC(hwnd, hdc);
            
            lastTime = currentTime;
        }
        else {
            // Sleep to reduce CPU usage
            Sleep(1);
        }
    }
    
    return 0;
}