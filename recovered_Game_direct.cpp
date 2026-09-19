Created At: 2026-05-26T17:51:58Z
Completed At: 2026-05-26T17:51:58Z
File Path: `file:///c:/Users/honco/OneDrive/Bureau/fallaga/src/Game.cpp`
Total Lines: 653
Total Bytes: 29879
Showing lines 1 to 653
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
1: #include <GL/glew.h>
2: #include <GLFW/glfw3.h>
3: #include <GL/glut.h>
4: #include "Game.h"
5: #include "Character.h"
6: #include "Weapon.h"
7: #include "Camera.h"
8: #include "Terrain.h"
9: #include "Horse.h"
10: #include <glm/glm.hpp>
11: #include <glm/gtc/matrix_transform.hpp>
12: #include <glm/gtc/type_ptr.hpp>
13: #include <iostream>
14: #include "Shader.h"
15: #include "CollisionSystem.h"
16: #include "AudioSystem.h"
17: #include <fstream>
18: 
19: #define WINDOW_WIDTH 800
20: #define WINDOW_HEIGHT 600
21: 
22: Game::Game() : lastFrameTime(0.0), deltaTime(0.0), totalTime(0.0) {
23:     AudioSystem::getInstance().init();
24:     player = new Character();
25:     terrain = new Terrain();
26:     camera = new Camera(player);
27:     particleSystem = new ParticleSystem();
28:     horse = new Horse();
29:     sky = new Sky();
30:     
31:     // Spawn player's primary car
32:     car = new Car();
33:     float gh = terrain->getHeight(10.0f, 10.0f);
34:     car->setPosition(Vec3(10.0f, gh + 1.0f, 10.0f));
35:     
36:     // Spawn traffic (Cars)
37:     // Some roaming, some parked
38:     npcCars.push_back(new Car(Car::State::ROAMING));
39:     npcCars.back()->setPosition(Vec3(-435, 12, 1315));
40:     
41:     npcCars.push_back(new Car(Car::State::ROAMING));
42:     npcCars.back()->setPosition(Vec3(-450, 12, 1330));
43:     npcCars.back()->setState(Car::State::ROAMING); // redundant but clear
44: 
45:     npcCars.push_back(new Car(Car::State::PARKED));
46:     npcCars.back()->setPosition(Vec3(-400, 12, 1280));
47: 
48:     npcC
<truncated 29465 bytes>
); break;
610:         case GLFW_KEY_A: player->keyUp('q'); break;
611:         case GLFW_KEY_D: player->keyUp('d'); break;
612:     }
613: }
614: 
615: void Game::mouseButton(int button, int action, double x, double y) {
616:     if (button == GLFW_MOUSE_BUTTON_RIGHT) {
617:         isRightMouseDown = (action == GLFW_PRESS);
618:     }
619: }
620: 
621: void Game::setupFBO(int width, int height) {
622:     if (sceneFBO) {
623:         glDeleteFramebuffers(1, &sceneFBO);
624:         glDeleteTextures(1, &sceneColorTex);
625:         glDeleteTextures(1, &sceneDepthTex);
626:     }
627: 
628:     glGenFramebuffers(1, &sceneFBO);
629:     glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
630: 
631:     // Color texture
632:     glGenTextures(1, &sceneColorTex);
633:     glBindTexture(GL_TEXTURE_2D, sceneColorTex);
634:     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
635:     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
636:     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
637:     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTex, 0);
638: 
639:     // Depth texture
640:     glGenTextures(1, &sceneDepthTex);
641:     glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
642:     glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
643:     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
644:     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
645:     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTex, 0);
646: 
647:     if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
648:         std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
649: 
650:     glBindFramebuffer(GL_FRAMEBUFFER, 0);
651: }
652: 
653: 
The above content shows the entire, complete file contents of the requested file.
