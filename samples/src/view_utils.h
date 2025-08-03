#ifndef APP_VIEW_H
#define APP_VIEW_H

#include "gl_renderer.h"
#include "camera_controller.h"
#include "app.h"

#include <imgui.h>

struct BaseViewComponent : AppComponent
{
	ResourceTable *table;

	RenderTargetID target;

	uint32_t w;
	uint32_t h;

	float fov = PIf/2.0f;
	float far = 100;
	float near = 0.01f;

	glm::dvec2 mouse_pos;
	glm::dvec2 dmouse_pos;

	BaseViewComponent(ResourceTable *table, int w, int h) 
	{
		this->table = table;
		this->w = (uint32_t)w;
		this->h = (uint32_t)h;

		RenderTargetCreateInfo target_info = {
			.w = (uint32_t)w,
			.h = (uint32_t)w,
			.flags = RENDER_TARGET_CREATE_COLOR_BIT | RENDER_TARGET_CREATE_DEPTH_BIT
		};
		target = render_target_create(table,&target_info);
		assert(target);
	}

	virtual void framebufferSizeCallback(int width, int height) override 
	{
		w = (uint32_t)width;
		h = (uint32_t)height;
		RenderTargetCreateInfo target_info = {
			.w = static_cast<uint32_t>(width),
			.h = static_cast<uint32_t>(height),
			.flags = RENDER_TARGET_CREATE_COLOR_BIT | RENDER_TARGET_CREATE_DEPTH_BIT
		};
		render_target_resize(table, target, &target_info);
		return;
	}

	virtual void cursorPosCallback(double xpos, double ypos) override 
	{
		static int init = 0;
		static double xold = 0, yold = 0;

		double xmid = 0.5*(double)w;
		double ymid = 0.5*(double)h;

		double x = (xpos - xmid)/xmid; 
		double y = -(ypos - ymid)/ymid; 

		double dx = x - xold;
		double dy = y - yold;

		xold = x;
		yold = y;

		printf("%f,%f\n",x,y);

		if (!init++)
			return;

		mouse_pos = glm::dvec2(x,y);
		dmouse_pos = glm::dvec2(dx,dy);
	}

	glm::mat4 get_proj()
	{
		ImGui::Begin("Demo Window");
		ImGui::SliderFloat("FOV", &fov, 0.0f, PI, "%.3f");
		ImGui::SliderFloat("near", &near, 0.0, 1.0, "%.5f");
		ImGui::SliderFloat("far", &far, near, 100, "%.5f");
		ImGui::End();

		float aspect = (float)h/(float)w;
		return camera_proj_3d(fov, aspect, far, near);
	}
};

struct SphereCameraComponent : AppComponent
{
	SphericalMotionCamera control;

	glm::dvec3 keydir = glm::dvec3(0);
	std::shared_ptr<const BaseViewComponent> view_component;

	SphereCameraComponent(std::shared_ptr<const BaseViewComponent> view) :
		view_component(view)
	{
	}

	virtual void keyCallback(int key, int scancode, int action, int mods) override
	{
		update_motion_wasd(keydir,key,action);
	}

	virtual void cursorPosCallback(double xpos, double ypos) override 
	{
		if (g_.mouse_mode == GLFW_CURSOR_DISABLED) {
			double dx = view_component->dmouse_pos.x;
			double dy = view_component->dmouse_pos.y;
			control.rotate(dy,dx);
		}
	}

	virtual void onFrameUpdateCallback() override
	{
		static float speed = 1;
		
		ImGui::Begin("Demo Window");
		ImGui::SliderFloat("speed", &speed, 0.0, 1, "%.5f");
		ImGui::End();

		control.min_height = view_component->near; 
		control.move((double)g_.dt*speed*keydir);
	}
};

struct MotionCameraComponent : AppComponent
{
	MotionCamera control;
	std::shared_ptr<const BaseViewComponent> view_component;

	MotionCameraComponent(std::shared_ptr<const BaseViewComponent> view) : 
		view_component(view)	
	{
		glm::vec3 eye = glm::vec3(2,0,0);
		control = MotionCamera::from_normal(glm::vec3(1,0,0),eye);
	}
	virtual void cursorPosCallback(double xpos, double ypos) override
	{
		if (g_.mouse_mode == GLFW_CURSOR_DISABLED) {
			double dx = view_component->dmouse_pos.x;
			double dy = view_component->dmouse_pos.y;
			control.rotate(dx,dy);
		}
	}

	virtual void onFrameUpdateCallback() override
	{
		static float speed = 1;
		
		ImGui::Begin("View Config");
		ImGui::SliderFloat("speed", &speed, 0.0, 1, "%.5f");
		ImGui::End();

		control.update(speed);
	}

	virtual void keyCallback(int key, int scancode, int action, int mods) override
	{
		control.handle_key_input(key, action);
	}
};


#endif //APP_VIEW_H
