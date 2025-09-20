#include "simulation.h"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define WIN_X 400
#define WIN_Y 400

int main()
{
    if (!glfwInit()) {
		fprintf(stderr, "ERROR: could not start GLFW3\n");
    }

    HopfSimulation sim("Hopf Simulation", WINDOW_WIDTH,WINDOW_HEIGHT,WIN_X,WIN_Y);

    glfwTerminate();
    return 0;
}
