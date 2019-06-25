/*
 * kernel/core/window.c
 *
 * Copyright(c) 2007-2019 Jianjun Jiang <8192542@qq.com>
 * Official site: http://xboot.org
 * Mobile phone: +86-18665388956
 * QQ: 8192542
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <xboot.h>
#include <input/input.h>
#include <input/keyboard.h>
#include <xboot/window.h>

static void fb_dummy_setbl(struct framebuffer_t * fb, int brightness)
{
}

static int fb_dummy_getbl(struct framebuffer_t * fb)
{
	return CONFIG_MAX_BRIGHTNESS;
}

static struct surface_t * fb_dummy_create(struct framebuffer_t * fb)
{
	return surface_alloc(fb->width, fb->height, NULL);
}

static void fb_dummy_destroy(struct framebuffer_t * fb, struct surface_t * s)
{
	surface_free(s);
}

static void fb_dummy_present(struct framebuffer_t * fb, struct surface_t * s, struct region_list_t * rl)
{
}

static struct framebuffer_t fb_dummy = {
	.name		= "fb-dummy",
	.width		= 640,
	.height		= 480,
	.pwidth		= 216,
	.pheight	= 135,
	.setbl		= fb_dummy_setbl,
	.getbl		= fb_dummy_getbl,
	.create		= fb_dummy_create,
	.destroy	= fb_dummy_destroy,
	.present	= fb_dummy_present,
	.priv		= NULL,
};

struct list_head __window_manager_list = {
	.next = &__window_manager_list,
	.prev = &__window_manager_list,
};
static spinlock_t __window_manager_lock = SPIN_LOCK_INIT();

static inline struct window_manager_t * window_manager_search(struct framebuffer_t * fb)
{
	struct window_manager_t * pos, * n;

	if(fb)
	{
		list_for_each_entry_safe(pos, n, &__window_manager_list, list)
		{
			if(pos->fb == fb)
				return pos;
		}
	}
	return NULL;
}

static struct window_manager_t * window_manager_alloc(const char * fb)
{
	struct window_manager_t * wm;
	struct framebuffer_t * dev;
	irq_flags_t flags;

	dev = search_framebuffer(fb);
	if(!dev)
	{
		dev = search_first_framebuffer();
		if(!dev)
			dev = &fb_dummy;
	}
	wm = window_manager_search(dev);
	if(wm)
		return wm;

	wm = malloc(sizeof(struct window_manager_t));
	if(!wm)
		return NULL;

	wm->fb = dev;
	wm->fifo = fifo_alloc(sizeof(struct event_t) * CONFIG_EVENT_FIFO_SIZE);
	wm->wcount = 0;
	wm->refresh = 0;
	spin_lock_init(&wm->lock);
	init_list_head(&wm->list);
	init_list_head(&wm->window);
	spin_lock_irqsave(&__window_manager_lock, flags);
	list_add_tail(&wm->list, &__window_manager_list);
	spin_unlock_irqrestore(&__window_manager_lock, flags);

	return wm;
}

static void window_manager_free(struct window_manager_t * wm)
{
	struct window_manager_t * pos, * n;
	irq_flags_t flags;

	if(!wm)
		return;

	list_for_each_entry_safe(pos, n, &__window_manager_list, list)
	{
		if(pos == wm)
		{
			spin_lock_irqsave(&__window_manager_lock, flags);
			list_del(&pos->list);
			spin_unlock_irqrestore(&__window_manager_lock, flags);
			fifo_free(pos->fifo);
			free(pos);
		}
	}
}

struct window_t * window_alloc(const char * fb, const char * input, void * data)
{
	struct window_manager_t * wm = window_manager_alloc(fb);
	struct window_t * w;
	struct input_t * dev;
	struct device_t * pos, * n;
	char *r, * p = (char *)input;
	int range[2];

	if(!wm)
		return NULL;

	w = malloc(sizeof(struct window_t));
	if(!w)
		return NULL;

	w->wm = wm;
	w->s = framebuffer_create_surface(w->wm->fb);
	w->rl = region_list_alloc(0);
	w->width = framebuffer_get_width(w->wm->fb);
	w->height = framebuffer_get_height(w->wm->fb);
	w->ashome = 0;
	w->showobj = 0;
	w->priv = data;
	if(p)
	{
		w->map = hmap_alloc(0);
		while((r = strsep(&p, ",;:|")) != NULL)
		{
			dev = search_input(r);
			if(dev)
			{
				hmap_add(w->map, r, dev);
				if(input_ioctl(dev, INPUT_IOCTL_MOUSE_GET_RANGE, &range[0]) >= 0)
				{
					range[0] = w->width;
					range[1] = w->height;
					input_ioctl(dev, INPUT_IOCTL_MOUSE_SET_RANGE, &range[0]);
				}
			}
		}
	}
	else
	{
		w->map = NULL;
		list_for_each_entry_safe(pos, n, &__device_head[DEVICE_TYPE_INPUT], head)
		{
			if(input_ioctl(pos->priv, INPUT_IOCTL_MOUSE_GET_RANGE, &range[0]) >= 0)
			{
				range[0] = w->width;
				range[1] = w->height;
				input_ioctl(pos->priv, INPUT_IOCTL_MOUSE_SET_RANGE, &range[0]);
			}
		}
	}
	spin_lock(&wm->lock);
	list_add_tail(&w->list, &wm->window);
	wm->wcount++;
	wm->refresh = 1;
	spin_unlock(&wm->lock);

	return w;
}

void window_free(struct window_t * w)
{
	if(!w || !w->wm)
		return;

	spin_lock(&w->wm->lock);
	list_del(&w->list);
	w->wm->wcount--;
	w->wm->refresh = 1;
	spin_unlock(&w->wm->lock);
	if(w->wm->wcount <= 0)
		window_manager_free(w->wm);
	hmap_free(w->map);
	surface_free(w->s);
	region_list_free(w->rl);
	free(w);
}

void window_to_front(struct window_t * w)
{
	if(w && w->wm && !list_is_last(&w->list, &w->wm->window))
	{
		spin_lock(&w->wm->lock);
		list_move_tail(&w->list, &w->wm->window);
		w->wm->refresh = 1;
		spin_unlock(&w->wm->lock);
	}
}

void window_to_back(struct window_t * w)
{
	if(w && w->wm && !list_is_first(&w->list, &w->wm->window))
	{
		spin_lock(&w->wm->lock);
		list_move(&w->list, &w->wm->window);
		w->wm->refresh = 1;
		spin_unlock(&w->wm->lock);
	}
}

void window_region_list_add(struct window_t * w, struct region_t * r)
{
	struct region_t region;

	if(w)
	{
		region_init(&region, 0, 0, w->width, w->height);
		if(region_intersect(&region, &region, r))
			region_list_add(w->rl, &region);
	}
}

void window_region_list_clear(struct window_t * w)
{
	if(w)
		region_list_clear(w->rl);
}

void window_present(struct window_t * w, void * o, void (*draw)(struct window_t *, void *))
{
	struct region_t rn, ro, * r;
	struct surface_t * s = w->s;
	int count;
	int i;

	if(w->wm->refresh)
	{
		region_init(&rn, 0, 0, w->width, w->height);
		window_region_list_add(w, &rn);
		w->wm->refresh = 0;
	}
	if((count = w->rl->count) > 0)
	{
		surface_shape_reset_clip(s);
		for(i = 0; i < count; i++)
		{
			r = &w->rl->region[i];
			surface_shape_rectangle(s, r->x, r->y, r->w, r->h);
		}
		surface_shape_clip(s);
		surface_shape_save(s);
		surface_shape_set_source_color(s, 1, 1, 1, 1);
		surface_shape_paint(s, 1);
		surface_shape_restore(s);
		if(draw)
			draw(w, o);
	}
	framebuffer_present_surface(w->wm->fb, w->s, w->rl);
}

int window_pump_event(struct window_t * w, struct event_t * e)
{
	if(w && e)
	{
		if(fifo_get(w->wm->fifo, (unsigned char *)e, sizeof(struct event_t)) == sizeof(struct event_t))
		{
			if(w->map)
				return hmap_search(w->map, ((struct input_t *)e->device)->name) ? 1 : 0;
			return 1;
		}
	}
	return 0;
}

void push_event(struct event_t * e)
{
	struct window_manager_t * pos, * n;
	struct window_t * wpos, * wn;

	if(e)
	{
		e->timestamp = ktime_get();
		list_for_each_entry_safe(pos, n, &__window_manager_list, list)
		{
			switch(e->type)
			{
			case EVENT_TYPE_KEY_DOWN:
				if((e->e.key_down.key == KEY_TASK) || (e->e.key_down.key == KEY_HOME))
				{
					list_for_each_entry_safe(wpos, wn, &pos->window, list)
					{
						if(wpos->ashome)
						{
							window_to_front(wpos);
							break;
						}
					}
				}
				break;
			case EVENT_TYPE_KEY_UP:
				break;
			default:
				break;
			}
			fifo_put(pos->fifo, (unsigned char *)e, sizeof(struct event_t));
		}
	}
}
