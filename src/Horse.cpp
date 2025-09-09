#define WIN32_LEAN_AND_MEAN
#include <GL/glew.h>
#include "Horse.h"


Horse::Horse() {}

void Horse::update() {}

void Horse::render() {
    float half = 0.25f;
    glPushMatrix();
    glTranslatef(1.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
        glVertex3f(-half,-half,-half); glVertex3f(half,-half,-half);
        glVertex3f(half,half,-half); glVertex3f(-half,half,-half);
        glVertex3f(-half,-half,half); glVertex3f(half,-half,half);
        glVertex3f(half,half,half); glVertex3f(-half,half,half);
    glEnd();
    glPopMatrix();
}
