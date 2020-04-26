#pragma once
#include "plugin.hpp"
#include "settings.hpp"

namespace StoermelderPackOne {
namespace Rack {

/** Move the view-port smoothly and center a Widget
 */
struct ViewportCenterSmooth {
	Vec source, target;
	float sourceZoom, targetZoom;
	int framecount = 0;
	int frame = 0;

	void trigger(Widget* w, float zoom, float framerate, float transitionTime = 1.f) {
		// source is at top-left, translate to center of screen
		Vec source = APP->scene->rackScroll->offset;
		source = source.plus(APP->scene->box.size.mult(0.5f));
		source = source.div(APP->scene->rackScroll->zoomWidget->zoom);

		Vec target = w->box.pos;
		target = target.plus(w->box.size.mult(0.5f));

		this->source = source;
		this->target = target;
		this->sourceZoom = rack::settings::zoom;
		this->targetZoom = zoom;
		this->framecount = int(transitionTime * framerate);
		this->frame = 0;
	}

	void process() {
		if (framecount == frame) return;

		float t = float(frame) / float(framecount);
		// Sigmoid
		t = t * 8.f - 4.f;
		t = 1.f / (1.f + std::exp(-t));
		t = rescale(t, 0.0179f, 0.98201f, 0.f, 1.f);

		// Calculate interpolated view-point and zoom
		Vec p1 = source.mult(1.f - t);
		Vec p2 = target.mult(t);
		Vec p = p1.plus(p2);
		float z = sourceZoom * (1.f - t) + targetZoom * t;

		// Move the view
		// NB: unstable API!
		rack::settings::zoom = z;
		p = p.mult(APP->scene->rackScroll->zoomWidget->zoom);
		p = p.minus(APP->scene->box.size.mult(0.5f));
		APP->scene->rackScroll->offset = p;

		frame++;
	}
};

struct ViewportCenterToWidget {
	ViewportCenterToWidget(Widget* w) {
		// NB: unstable API!
		Vec target = w->box.pos;
		target = target.plus(w->box.size.mult(0.5f));
		target = target.mult(APP->scene->rackScroll->zoomWidget->zoom);
		target = target.minus(APP->scene->box.size.mult(0.5f));
		APP->scene->rackScroll->offset = target;
	}
};

} // namespace Rack
} // namespace StoermelderPackOne