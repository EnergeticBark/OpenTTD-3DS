/* $Id$ */

/** @file window.cpp Windowing system, widgets and events */

#include "stdafx.h"
#include <stdarg.h>
#include "openttd.h"
#include "company_func.h"
#include "gfx_func.h"
#include "console_func.h"
#include "console_gui.h"
#include "viewport_func.h"
#include "variables.h"
#include "genworld.h"
#include "blitter/factory.hpp"
#include "zoom_func.h"
#include "map_func.h"
#include "vehicle_base.h"
#include "settings_type.h"
#include "cheat_type.h"
#include "window_func.h"
#include "tilehighlight_func.h"
#include "network/network.h"
#include "querystring_gui.h"
#include "widgets/dropdown_func.h"

#include "table/sprites.h"

static Point _drag_delta; ///< delta between mouse cursor and upper left corner of dragged window
static Window *_mouseover_last_w = NULL; ///< Window of the last MOUSEOVER event

/** List of windows opened at the screen sorted from the front. */
Window *_z_front_window = NULL;
/** List of windows opened at the screen sorted from the back. */
Window *_z_back_window  = NULL;

/*
 * Window that currently have focus. - The main purpose is to generate
 * FocusLost events, not to give next window in z-order focus when a
 * window is closed.
 */
Window *_focused_window;

Point _cursorpos_drag_start;

int _scrollbar_start_pos;
int _scrollbar_size;
byte _scroller_click_timeout;

bool _scrolling_scrollbar;
bool _scrolling_viewport;

byte _special_mouse_mode;

/** Window description constructor. */
WindowDesc::WindowDesc(int16 left, int16 top, int16 min_width, int16 min_height, int16 def_width, int16 def_height,
			WindowClass window_class, WindowClass parent_class, uint32 flags, const Widget *widgets)
{
	this->left = left;
	this->top = top;
	this->minimum_width = min_width;
	this->minimum_height = min_height;
	this->default_width = def_width;
	this->default_height = def_height;
	this->cls = window_class;
	this->parent_cls = parent_class;
	this->flags = flags;
	this->widgets = widgets;
}


/**
 * Set the window that has the focus
 * @param w The window to set the focus on
 */
void SetFocusedWindow(Window *w)
{
	if (_focused_window == w) return;

	/* Invalidate focused widget */
	if (_focused_window != NULL && _focused_window->focused_widget != NULL) {
		uint focused_widget_id = _focused_window->focused_widget - _focused_window->widget;
		_focused_window->InvalidateWidget(focused_widget_id);
	}

	/* Remember which window was previously focused */
	Window *old_focused = _focused_window;
	_focused_window = w;

	/* So we can inform it that it lost focus */
	if (old_focused != NULL) old_focused->OnFocusLost();
	if (_focused_window != NULL) _focused_window->OnFocus();
}

/**
 * Gets the globally focused widget. Which is the focused widget of the focused window.
 * @return A pointer to the globally focused Widget, or NULL if there is no globally focused widget.
 */
const Widget *GetGloballyFocusedWidget()
{
	return _focused_window != NULL ? _focused_window->focused_widget : NULL;
}

/**
 * Check if an edit box is in global focus. That is if focused window
 * has a edit box as focused widget, or if a console is focused.
 * @return returns true if an edit box is in global focus or if the focused window is a console, else false
 */
bool EditBoxInGlobalFocus()
{
	const Widget *wi = GetGloballyFocusedWidget();

	/* The console does not have an edit box so a special case is needed. */
	return (wi != NULL && wi->type == WWT_EDITBOX) ||
			(_focused_window != NULL && _focused_window->window_class == WC_CONSOLE);
}

/**
 * Sets the enabled/disabled status of a list of widgets.
 * By default, widgets are enabled.
 * On certain conditions, they have to be disabled.
 * @param disab_stat status to use ie: disabled = true, enabled = false
 * @param widgets list of widgets ended by WIDGET_LIST_END
 */
void CDECL Window::SetWidgetsDisabledState(bool disab_stat, int widgets, ...)
{
	va_list wdg_list;

	va_start(wdg_list, widgets);

	while (widgets != WIDGET_LIST_END) {
		SetWidgetDisabledState(widgets, disab_stat);
		widgets = va_arg(wdg_list, int);
	}

	va_end(wdg_list);
}

/**
 * Sets the hidden/shown status of a list of widgets.
 * By default, widgets are visible.
 * On certain conditions, they have to be hidden.
 * @param hidden_stat status to use ie. hidden = true, visible = false
 * @param widgets list of widgets ended by WIDGET_LIST_END
 */
void CDECL Window::SetWidgetsHiddenState(bool hidden_stat, int widgets, ...)
{
	va_list wdg_list;

	va_start(wdg_list, widgets);

	while (widgets != WIDGET_LIST_END) {
		SetWidgetHiddenState(widgets, hidden_stat);
		widgets = va_arg(wdg_list, int);
	}

	va_end(wdg_list);
}

/**
 * Sets the lowered/raised status of a list of widgets.
 * @param lowered_stat status to use ie: lowered = true, raised = false
 * @param widgets list of widgets ended by WIDGET_LIST_END
 */
void CDECL Window::SetWidgetsLoweredState(bool lowered_stat, int widgets, ...)
{
	va_list wdg_list;

	va_start(wdg_list, widgets);

	while (widgets != WIDGET_LIST_END) {
		SetWidgetLoweredState(widgets, lowered_stat);
		widgets = va_arg(wdg_list, int);
	}

	va_end(wdg_list);
}

/**
 * Raise all buttons of the window
 */
void Window::RaiseButtons()
{
	for (uint i = 0; i < this->widget_count; i++) {
		if (this->IsWidgetLowered(i)) {
			this->RaiseWidget(i);
			this->InvalidateWidget(i);
		}
	}
}

/**
 * Invalidate a widget, i.e. mark it as being changed and in need of redraw.
 * @param widget_index the widget to redraw.
 */
void Window::InvalidateWidget(byte widget_index) const
{
	const Widget *wi = &this->widget[widget_index];

	/* Don't redraw the window if the widget is invisible or of no-type */
	if (wi->type == WWT_EMPTY || IsWidgetHidden(widget_index)) return;

	SetDirtyBlocks(this->left + wi->left, this->top + wi->top, this->left + wi->right + 1, this->top + wi->bottom + 1);
}

/**
 * Do all things to make a button look clicked and mark it to be
 * unclicked in a few ticks.
 * @param widget the widget to "click"
 */
void Window::HandleButtonClick(byte widget)
{
	this->LowerWidget(widget);
	this->flags4 |= WF_TIMEOUT_BEGIN;
	this->InvalidateWidget(widget);
}

/**
 * Checks if the window has at least one widget of given type
 * @param widget_type the widget type to look for
 */
bool Window::HasWidgetOfType(WidgetType widget_type) const
{
	for (uint i = 0; i < this->widget_count; i++) {
		if (this->widget[i].type == widget_type) return true;
	}
	return false;
}

static void StartWindowDrag(Window *w);
static void StartWindowSizing(Window *w);

/**
 * Dispatch left mouse-button (possibly double) click in window.
 * @param w Window to dispatch event in
 * @param x X coordinate of the click
 * @param y Y coordinate of the click
 * @param double_click Was it a double click?
 */
static void DispatchLeftClickEvent(Window *w, int x, int y, bool double_click)
{
	bool focused_widget_changed = false;
	int widget = 0;
	if (w->desc_flags & WDF_DEF_WIDGET) {
		widget = GetWidgetFromPos(w, x, y);

		/* If clicked on a window that previously did dot have focus */
		if (_focused_window != w &&
				(w->desc_flags & WDF_NO_FOCUS) == 0 &&           // Don't lose focus to toolbars
				!(w->desc_flags & WDF_STD_BTN && widget == 0)) { // Don't change focused window if 'X' (close button) was clicked
			focused_widget_changed = true;
			if (_focused_window != NULL) {
				_focused_window->OnFocusLost();

				/* The window that lost focus may have had opened a OSK, window so close it, unless the user has clicked on the OSK window. */
				if (w->window_class != WC_OSK) DeleteWindowById(WC_OSK, 0);
			}
			SetFocusedWindow(w);
			w->OnFocus();
		}

		if (widget < 0) return; // exit if clicked outside of widgets

		/* don't allow any interaction if the button has been disabled */
		if (w->IsWidgetDisabled(widget)) return;

		const Widget *wi = &w->widget[widget];

		/* Clicked on a widget that is not disabled.
		 * So unless the clicked widget is the caption bar, change focus to this widget */
		if (wi->type != WWT_CAPTION) {
			/* Close the OSK window if a edit box loses focus */
			if (w->focused_widget && w->focused_widget->type == WWT_EDITBOX && // An edit box was previously selected
					w->focused_widget != wi &&                                 // and focus is going to change
					w->window_class != WC_OSK) {                               // and it is not the OSK window
				DeleteWindowById(WC_OSK, 0);
			}

			if (w->focused_widget != wi) {
				/* Repaint the widget that loss focus. A focused edit box may else leave the caret left on the screen */
				if (w->focused_widget) w->InvalidateWidget(w->focused_widget - w->widget);
				focused_widget_changed = true;
				w->focused_widget = wi;
			}
		}

		if (wi->type & WWB_MASK) {
			/* special widget handling for buttons*/
			switch (wi->type) {
				default: NOT_REACHED();
				case WWT_PANEL   | WWB_PUSHBUTTON: // WWT_PUSHBTN
				case WWT_IMGBTN  | WWB_PUSHBUTTON: // WWT_PUSHIMGBTN
				case WWT_TEXTBTN | WWB_PUSHBUTTON: // WWT_PUSHTXTBTN
					w->HandleButtonClick(widget);
					break;
			}
		} else if (wi->type == WWT_SCROLLBAR || wi->type == WWT_SCROLL2BAR || wi->type == WWT_HSCROLLBAR) {
			ScrollbarClickHandler(w, wi, x, y);
		} else if (wi->type == WWT_EDITBOX && !focused_widget_changed) { // Only open the OSK window if clicking on an already focused edit box
			/* Open the OSK window if clicked on an edit box */
			QueryStringBaseWindow *qs = dynamic_cast<QueryStringBaseWindow*>(w);
			if (qs != NULL) {
				const int widget_index = wi - w->widget;
				qs->OnOpenOSKWindow(widget_index);
			}
		}

		/* Close any child drop down menus. If the button pressed was the drop down
		 * list's own button, then we should not process the click any further. */
		if (HideDropDownMenu(w) == widget) return;

		if (w->desc_flags & WDF_STD_BTN) {
			if (widget == 0) { // 'X'
				delete w;
				return;
			}

			if (widget == 1) { // 'Title bar'
				StartWindowDrag(w);
				return;
			}
		}

		if (w->desc_flags & WDF_RESIZABLE && wi->type == WWT_RESIZEBOX) {
			StartWindowSizing(w);
			w->InvalidateWidget(widget);
			return;
		}

		if (w->desc_flags & WDF_STICKY_BUTTON && wi->type == WWT_STICKYBOX) {
			w->flags4 ^= WF_STICKY;
			w->InvalidateWidget(widget);
			return;
		}
	}

	Point pt = { x, y };

	if (double_click) {
		w->OnDoubleClick(pt, widget);
	} else {
		w->OnClick(pt, widget);
	}
}

/**
 * Dispatch right mouse-button click in window.
 * @param w Window to dispatch event in
 * @param x X coordinate of the click
 * @param y Y coordinate of the click
 */
static void DispatchRightClickEvent(Window *w, int x, int y)
{
	int widget = 0;

	/* default tooltips handler? */
	if (w->desc_flags & WDF_STD_TOOLTIPS) {
		widget = GetWidgetFromPos(w, x, y);
		if (widget < 0) return; // exit if clicked outside of widgets

		if (w->widget[widget].tooltips != 0) {
			GuiShowTooltips(w->widget[widget].tooltips);
			return;
		}
	}

	Point pt = { x, y };
	w->OnRightClick(pt, widget);
}

/**
 * Dispatch the mousewheel-action to the window.
 * The window will scroll any compatible scrollbars if the mouse is pointed over the bar or its contents
 * @param w Window
 * @param widget the widget where the scrollwheel was used
 * @param wheel scroll up or down
 */
static void DispatchMouseWheelEvent(Window *w, int widget, int wheel)
{
	if (widget < 0) return;

	const Widget *wi1 = &w->widget[widget];
	const Widget *wi2 = &w->widget[widget + 1];

	/* The listbox can only scroll if scrolling was done on the scrollbar itself,
	 * or on the listbox (and the next item is (must be) the scrollbar)
	 * XXX - should be rewritten as a widget-dependent scroller but that's
	 * not happening until someone rewrites the whole widget-code */
	Scrollbar *sb;
	if ((sb = &w->vscroll,  wi1->type == WWT_SCROLLBAR)  || (sb = &w->vscroll2, wi1->type == WWT_SCROLL2BAR)  ||
			(sb = &w->vscroll2, wi2->type == WWT_SCROLL2BAR) || (sb = &w->vscroll, wi2->type == WWT_SCROLLBAR) ) {

		if (sb->count > sb->cap) {
			int pos = Clamp(sb->pos + wheel, 0, sb->count - sb->cap);
			if (pos != sb->pos) {
				sb->pos = pos;
				w->SetDirty();
			}
		}
	}
}

/**
 * Generate repaint events for the visible part of window w within the rectangle.
 *
 * The function goes recursively upwards in the window stack, and splits the rectangle
 * into multiple pieces at the window edges, so obscured parts are not redrawn.
 *
 * @param w Window that needs to be repainted
 * @param left Left edge of the rectangle that should be repainted
 * @param top Top edge of the rectangle that should be repainted
 * @param right Right edge of the rectangle that should be repainted
 * @param bottom Bottom edge of the rectangle that should be repainted
 */
static void DrawOverlappedWindow(Window *w, int left, int top, int right, int bottom)
{
	const Window *v;
	FOR_ALL_WINDOWS_FROM_BACK_FROM(v, w->z_front) {
		if (right > v->left &&
				bottom > v->top &&
				left < v->left + v->width &&
				top < v->top + v->height) {
			/* v and rectangle intersect with eeach other */
			int x;

			if (left < (x = v->left)) {
				DrawOverlappedWindow(w, left, top, x, bottom);
				DrawOverlappedWindow(w, x, top, right, bottom);
				return;
			}

			if (right > (x = v->left + v->width)) {
				DrawOverlappedWindow(w, left, top, x, bottom);
				DrawOverlappedWindow(w, x, top, right, bottom);
				return;
			}

			if (top < (x = v->top)) {
				DrawOverlappedWindow(w, left, top, right, x);
				DrawOverlappedWindow(w, left, x, right, bottom);
				return;
			}

			if (bottom > (x = v->top + v->height)) {
				DrawOverlappedWindow(w, left, top, right, x);
				DrawOverlappedWindow(w, left, x, right, bottom);
				return;
			}

			return;
		}
	}

	/* Setup blitter, and dispatch a repaint event to window *wz */
	DrawPixelInfo *dp = _cur_dpi;
	dp->width = right - left;
	dp->height = bottom - top;
	dp->left = left - w->left;
	dp->top = top - w->top;
	dp->pitch = _screen.pitch;
	dp->dst_ptr = BlitterFactoryBase::GetCurrentBlitter()->MoveTo(_screen.dst_ptr, left, top);
	dp->zoom = ZOOM_LVL_NORMAL;
	w->OnPaint();
}

/**
 * From a rectangle that needs redrawing, find the windows that intersect with the rectangle.
 * These windows should be re-painted.
 * @param left Left edge of the rectangle that should be repainted
 * @param top Top edge of the rectangle that should be repainted
 * @param right Right edge of the rectangle that should be repainted
 * @param bottom Bottom edge of the rectangle that should be repainted
 */
void DrawOverlappedWindowForAll(int left, int top, int right, int bottom)
{
	Window *w;
	DrawPixelInfo bk;
	_cur_dpi = &bk;

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (right > w->left &&
				bottom > w->top &&
				left < w->left + w->width &&
				top < w->top + w->height) {
			/* Window w intersects with the rectangle => needs repaint */
			DrawOverlappedWindow(w, left, top, right, bottom);
		}
	}
}

/**
 * Mark entire window as dirty (in need of re-paint)
 * @ingroup dirty
 */
void Window::SetDirty() const
{
	SetDirtyBlocks(this->left, this->top, this->left + this->width, this->top + this->height);
}

/**
 * Mark entire window as dirty (in need of re-paint)
 * @param w Window to redraw
 * @ingroup dirty
 */
void SetWindowDirty(const Window *w)
{
	if (w != NULL) w->SetDirty();
}

/** Find the Window whose parent pointer points to this window
 * @param w parent Window to find child of
 * @return a Window pointer that is the child of w, or NULL otherwise */
static Window *FindChildWindow(const Window *w)
{
	Window *v;
	FOR_ALL_WINDOWS_FROM_BACK(v) {
		if (v->parent == w) return v;
	}

	return NULL;
}

/**
 * Delete all children a window might have in a head-recursive manner
 */
void Window::DeleteChildWindows() const
{
	Window *child = FindChildWindow(this);
	while (child != NULL) {
		delete child;
		child = FindChildWindow(this);
	}
}

/**
 * Remove window and all its child windows from the window stack.
 */
Window::~Window()
{
	if (_thd.place_mode != VHM_NONE &&
			_thd.window_class == this->window_class &&
			_thd.window_number == this->window_number) {
		ResetObjectToPlace();
	}

	/* Prevent Mouseover() from resetting mouse-over coordinates on a non-existing window */
	if (_mouseover_last_w == this) _mouseover_last_w = NULL;

	/* Make sure we don't try to access this window as the focused window when it don't exist anymore. */
	if (_focused_window == this) _focused_window = NULL;

	this->DeleteChildWindows();

	if (this->viewport != NULL) DeleteWindowViewport(this);

	this->SetDirty();

	free(this->widget);

	/* N3DS. 
	 * Backported this line from a later version of OpenTTD so optimizations will no longer break on new versions of GCC */
	const_cast<volatile WindowClass &>(this->window_class) = WC_INVALID;
}

/**
 * Find a window by its class and window number
 * @param cls Window class
 * @param number Number of the window within the window class
 * @return Pointer to the found window, or \c NULL if not available
 */
Window *FindWindowById(WindowClass cls, WindowNumber number)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) return w;
	}

	return NULL;
}

/**
 * Delete a window by its class and window number (if it is open).
 * @param cls Window class
 * @param number Number of the window within the window class
 * @param force force deletion; if false don't delete when stickied
 */
void DeleteWindowById(WindowClass cls, WindowNumber number, bool force)
{
	Window *w = FindWindowById(cls, number);
	if (force || w == NULL ||
			(w->desc_flags & WDF_STICKY_BUTTON) == 0 ||
			(w->flags4 & WF_STICKY) == 0) {
		delete w;
	}
}

/**
 * Delete all windows of a given class
 * @param cls Window class of windows to delete
 */
void DeleteWindowByClass(WindowClass cls)
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) {
			delete w;
			goto restart_search;
		}
	}
}

/** Delete all windows of a company. We identify windows of a company
 * by looking at the caption colour. If it is equal to the company ID
 * then we say the window belongs to the company and should be deleted
 * @param id company identifier */
void DeleteCompanyWindows(CompanyID id)
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->owner == id) {
			delete w;
			goto restart_search;
		}
	}

	/* Also delete the company specific windows, that don't have a company-colour */
	DeleteWindowById(WC_BUY_COMPANY, id);
}

/** Change the owner of all the windows one company can take over from another
 * company in the case of a company merger. Do not change ownership of windows
 * that need to be deleted once takeover is complete
 * @param old_owner original owner of the window
 * @param new_owner the new owner of the window */
void ChangeWindowOwner(Owner old_owner, Owner new_owner)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->owner != old_owner) continue;

		switch (w->window_class) {
			case WC_COMPANY_COLOUR:
			case WC_FINANCES:
			case WC_STATION_LIST:
			case WC_TRAINS_LIST:
			case WC_ROADVEH_LIST:
			case WC_SHIPS_LIST:
			case WC_AIRCRAFT_LIST:
			case WC_BUY_COMPANY:
			case WC_COMPANY:
				continue;

			default:
				w->owner = new_owner;
				break;
		}
	}
}

static void BringWindowToFront(Window *w);

/** Find a window and make it the top-window on the screen. The window
 * gets a white border for a brief period of time to visualize its "activation"
 * @param cls WindowClass of the window to activate
 * @param number WindowNumber of the window to activate
 * @return a pointer to the window thus activated */
Window *BringWindowToFrontById(WindowClass cls, WindowNumber number)
{
	Window *w = FindWindowById(cls, number);

	if (w != NULL) {
		w->flags4 |= WF_WHITE_BORDER_MASK;
		BringWindowToFront(w);
		w->SetDirty();
	}

	return w;
}

static inline bool IsVitalWindow(const Window *w)
{
	switch (w->window_class) {
		case WC_MAIN_TOOLBAR:
		case WC_STATUS_BAR:
		case WC_NEWS_WINDOW:
		case WC_SEND_NETWORK_MSG:
			return true;

		default:
			return false;
	}
}

/** On clicking on a window, make it the frontmost window of all. However
 * there are certain windows that always need to be on-top; these include
 * - Toolbar, Statusbar (always on)
 * - New window, Chatbar (only if open)
 * The window is marked dirty for a repaint if the window is actually moved
 * @param w window that is put into the foreground
 * @return pointer to the window, the same as the input pointer
 */
static void BringWindowToFront(Window *w)
{
	Window *v = _z_front_window;

	/* Bring the window just below the vital windows */
	for (; v != NULL && v != w && IsVitalWindow(v); v = v->z_back) { }

	if (v == NULL || w == v) return; // window is already in the right position

	/* w cannot be at the top already! */
	assert(w != _z_front_window);

	if (w->z_back == NULL) {
		_z_back_window = w->z_front;
	} else {
		w->z_back->z_front = w->z_front;
	}
	w->z_front->z_back = w->z_back;

	w->z_front = v->z_front;
	w->z_back = v;

	if (v->z_front == NULL) {
		_z_front_window = w;
	} else {
		v->z_front->z_back = w;
	}
	v->z_front = w;

	w->SetDirty();
}

/**
 * Assign widgets to a new window by initialising its widget pointers, and by
 * copying the widget array \a widget to \c w->widget to allow for resizable
 * windows.
 * @param w Window on which to attach the widget array
 * @param widget pointer of widget array to fill the window with
 *
 * @post \c w->widget points to allocated memory and contains the copied widget array except for the terminating widget,
 *       \c w->widget_count contains number of widgets in the allocated memory.
 */
static void AssignWidgetToWindow(Window *w, const Widget *widget)
{
	if (widget != NULL) {
		uint index = 1;

		for (const Widget *wi = widget; wi->type != WWT_LAST; wi++) index++;

		w->widget = MallocT<Widget>(index);
		memcpy(w->widget, widget, sizeof(*w->widget) * index);
		w->widget_count = index - 1;
	} else {
		w->widget = NULL;
		w->widget_count = 0;
	}
}

/**
 * Initializes a new Window.
 * This function is called the constructors.
 * See descriptions for those functions for usage
 * Only addition here is window_number, which is the window_number being assigned to the new window
 * @param x offset in pixels from the left of the screen
 * @param y offset in pixels from the top of the screen
 * @param min_width minimum width in pixels of the window
 * @param min_height minimum height in pixels of the window
 * @param cls see WindowClass class of the window, used for identification and grouping
 * @param *widget see Widget pointer to the window layout and various elements
 * @param window_number number being assigned to the new window
 * @return Window pointer of the newly created window
 */
void Window::Initialize(int x, int y, int min_width, int min_height,
				WindowClass cls, const Widget *widget, int window_number)
{
	/* Set up window properties */
	this->window_class = cls;
	this->flags4 = WF_WHITE_BORDER_MASK; // just opened windows have a white border
	this->owner = INVALID_OWNER;
	this->left = x;
	this->top = y;
	this->width = min_width;
	this->height = min_height;
	AssignWidgetToWindow(this, widget);
	this->focused_widget = 0;
	this->resize.width = min_width;
	this->resize.height = min_height;
	this->resize.step_width = 1;
	this->resize.step_height = 1;
	this->window_number = window_number;

	/* Give focus to the opened window unless it is the OSK window or a text box
	 * of focused window has focus (so we don't interrupt typing). But if the new
	 * window has a text box, then take focus anyway. */
	if (this->window_class != WC_OSK && (!EditBoxInGlobalFocus() || this->HasWidgetOfType(WWT_EDITBOX))) SetFocusedWindow(this);

	/* Hacky way of specifying always-on-top windows. These windows are
	 * always above other windows because they are moved below them.
	 * status-bar is above news-window because it has been created earlier.
	 * Also, as the chat-window is excluded from this, it will always be
	 * the last window, thus always on top.
	 * XXX - Yes, ugly, probably needs something like w->always_on_top flag
	 * to implement correctly, but even then you need some kind of distinction
	 * between on-top of chat/news and status windows, because these conflict */
	Window *w = _z_front_window;
	if (w != NULL && this->window_class != WC_SEND_NETWORK_MSG && this->window_class != WC_HIGHSCORE && this->window_class != WC_ENDSCREEN) {
		if (FindWindowById(WC_MAIN_TOOLBAR, 0)     != NULL) w = w->z_back;
		if (FindWindowById(WC_STATUS_BAR, 0)       != NULL) w = w->z_back;
		if (FindWindowById(WC_NEWS_WINDOW, 0)      != NULL) w = w->z_back;
		if (FindWindowById(WC_SEND_NETWORK_MSG, 0) != NULL) w = w->z_back;

		if (w == NULL) {
			_z_back_window->z_front = this;
			this->z_back = _z_back_window;
			_z_back_window = this;
		} else {
			if (w->z_front == NULL) {
				_z_front_window = this;
			} else {
				this->z_front = w->z_front;
				w->z_front->z_back = this;
			}

			this->z_back = w;
			w->z_front = this;
		}
	} else {
		this->z_back = _z_front_window;
		if (_z_front_window != NULL) {
			_z_front_window->z_front = this;
		} else {
			_z_back_window = this;
		}
		_z_front_window = this;
	}
}

/**
 * Resize window towards the default size.
 * Prior to construction, a position for the new window (for its default size)
 * has been found with LocalGetWindowPlacement(). Initially, the window is
 * constructed with minimal size. Resizing the window to its default size is
 * done here.
 * @param def_width default width in pixels of the window
 * @param def_height default height in pixels of the window
 * @see Window::Window(), Window::Initialize()
 */
void Window::FindWindowPlacementAndResize(int def_width, int def_height)
{
	/* Try to make windows smaller when our window is too small.
	 * w->(width|height) is normally the same as min_(width|height),
	 * but this way the GUIs can be made a little more dynamic;
	 * one can use the same spec for multiple windows and those
	 * can then determine the real minimum size of the window. */
	if (this->width != def_width || this->height != def_height) {
		/* Think about the overlapping toolbars when determining the minimum window size */
		int free_height = _screen.height;
		const Window *wt = FindWindowById(WC_STATUS_BAR, 0);
		if (wt != NULL) free_height -= wt->height;
		wt = FindWindowById(WC_MAIN_TOOLBAR, 0);
		if (wt != NULL) free_height -= wt->height;

		int enlarge_x = max(min(def_width  - this->width,  _screen.width - this->width),  0);
		int enlarge_y = max(min(def_height - this->height, free_height   - this->height), 0);

		/* X and Y has to go by step.. calculate it.
		 * The cast to int is necessary else x/y are implicitly casted to
		 * unsigned int, which won't work. */
		if (this->resize.step_width  > 1) enlarge_x -= enlarge_x % (int)this->resize.step_width;
		if (this->resize.step_height > 1) enlarge_y -= enlarge_y % (int)this->resize.step_height;

		ResizeWindow(this, enlarge_x, enlarge_y);

		Point size;
		Point diff;
		size.x = this->width;
		size.y = this->height;
		diff.x = enlarge_x;
		diff.y = enlarge_y;
		this->OnResize(size, diff);
	}

	int nx = this->left;
	int ny = this->top;

	if (nx + this->width > _screen.width) nx -= (nx + this->width - _screen.width);

	const Window *wt = FindWindowById(WC_MAIN_TOOLBAR, 0);
	ny = max(ny, (wt == NULL || this == wt || this->top == 0) ? 0 : wt->height);
	nx = max(nx, 0);

	if (this->viewport != NULL) {
		this->viewport->left += nx - this->left;
		this->viewport->top  += ny - this->top;
	}
	this->left = nx;
	this->top = ny;

	this->SetDirty();
}

/**
 * Resize window towards the default size given in the window description.
 * @param desc the description to get the default size from.
 */
void Window::FindWindowPlacementAndResize(const WindowDesc *desc)
{
	this->FindWindowPlacementAndResize(desc->default_width, desc->default_height);
}

/**
 * Open a new window. If there is no space for a new window, close an open
 * window. Try to avoid stickied windows, but if there is no else, close one of
 * those as well. Then make sure all created windows are below some always-on-top
 * ones. Finally set all variables and call the WE_CREATE event
 * @param x offset in pixels from the left of the screen
 * @param y offset in pixels from the top of the screen
 * @param width width in pixels of the window
 * @param height height in pixels of the window
 * @param cls see WindowClass class of the window, used for identification and grouping
 * @param *widget see Widget pointer to the window layout and various elements
 * @return Window pointer of the newly created window
 */
Window::Window(int x, int y, int width, int height, WindowClass cls, const Widget *widget)
{
	this->Initialize(x, y, width, height, cls, widget, 0);
}

/**
 * Decide whether a given rectangle is a good place to open a completely visible new window.
 * The new window should be within screen borders, and not overlap with another already
 * existing window (except for the main window in the background).
 * @param left    Left edge of the rectangle
 * @param top     Top edge of the rectangle
 * @param width   Width of the rectangle
 * @param height  Height of the rectangle
 * @param pos     If rectangle is good, use this parameter to return the top-left corner of the new window
 * @return Boolean indication that the rectangle is a good place for the new window
 */
static bool IsGoodAutoPlace1(int left, int top, int width, int height, Point &pos)
{
	int right  = width + left;
	int bottom = height + top;

	if (left < 0 || top < 22 || right > _screen.width || bottom > _screen.height) return false;

	/* Make sure it is not obscured by any window. */
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (right > w->left &&
				w->left + w->width > left &&
				bottom > w->top &&
				w->top + w->height > top) {
			return false;
		}
	}

	pos.x = left;
	pos.y = top;
	return true;
}

/**
 * Decide whether a given rectangle is a good place to open a mostly visible new window.
 * The new window should be mostly within screen borders, and not overlap with another already
 * existing window (except for the main window in the background).
 * @param left    Left edge of the rectangle
 * @param top     Top edge of the rectangle
 * @param width   Width of the rectangle
 * @param height  Height of the rectangle
 * @param pos     If rectangle is good, use this parameter to return the top-left corner of the new window
 * @return Boolean indication that the rectangle is a good place for the new window
 */
static bool IsGoodAutoPlace2(int left, int top, int width, int height, Point &pos)
{
	/* Left part of the rectangle may be at most 1/4 off-screen,
	 * right part of the rectangle may be at most 1/2 off-screen
	 */
	if (left < -(width>>2) || left > _screen.width - (width>>1)) return false;
	/* Bottom part of the rectangle may be at most 1/4 off-screen */
	if (top < 22 || top > _screen.height - (height>>2)) return false;

	/* Make sure it is not obscured by any window. */
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (left + width > w->left &&
				w->left + w->width > left &&
				top + height > w->top &&
				w->top + w->height > top) {
			return false;
		}
	}

	pos.x = left;
	pos.y = top;
	return true;
}

/**
 * Find a good place for opening a new window of a given width and height.
 * @param width  Width of the new window
 * @param height Height of the new window
 * @return Top-left coordinate of the new window
 */
static Point GetAutoPlacePosition(int width, int height)
{
	Point pt;

	/* First attempt, try top-left of the screen */
	if (IsGoodAutoPlace1(0, 24, width, height, pt)) return pt;

	/* Second attempt, try around all existing windows with a distance of 2 pixels.
	 * The new window must be entirely on-screen, and not overlap with an existing window.
	 * Eight starting points are tried, two at each corner.
	 */
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (IsGoodAutoPlace1(w->left + w->width + 2, w->top, width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left - width - 2,    w->top, width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left, w->top + w->height + 2, width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left, w->top - height - 2,    width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left + w->width + 2, w->top + w->height - height, width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left - width - 2,    w->top + w->height - height, width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left + w->width - width, w->top + w->height + 2, width, height, pt)) return pt;
		if (IsGoodAutoPlace1(w->left + w->width - width, w->top - height - 2,    width, height, pt)) return pt;
	}

	/* Third attempt, try around all existing windows with a distance of 2 pixels.
	 * The new window may be partly off-screen, and must not overlap with an existing window.
	 * Only four starting points are tried.
	 */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == WC_MAIN_WINDOW) continue;

		if (IsGoodAutoPlace2(w->left + w->width + 2, w->top, width, height, pt)) return pt;
		if (IsGoodAutoPlace2(w->left - width - 2,    w->top, width, height, pt)) return pt;
		if (IsGoodAutoPlace2(w->left, w->top + w->height + 2, width, height, pt)) return pt;
		if (IsGoodAutoPlace2(w->left, w->top - height - 2,    width, height, pt)) return pt;
	}

	/* Fourth and final attempt, put window at diagonal starting from (0, 24), try multiples
	 * of (+5, +5)
	 */
	int left = 0, top = 24;

restart:
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->left == left && w->top == top) {
			left += 5;
			top += 5;
			goto restart;
		}
	}

	pt.x = left;
	pt.y = top;
	return pt;
}

/**
 * Compute the position of the top-left corner of a new window that is opened.
 *
 * By default position a child window at an offset of 10/10 of its parent.
 * With the exception of WC_BUILD_TOOLBAR (build railway/roads/ship docks/airports)
 * and WC_SCEN_LAND_GEN (landscaping). Whose child window has an offset of 0/36 of
 * its parent. So it's exactly under the parent toolbar and no buttons will be covered.
 * However if it falls too extremely outside window positions, reposition
 * it to an automatic place.
 *
 * @param *desc         The pointer to the WindowDesc to be created
 * @param window_number the window number of the new window
 *
 * @return Coordinate of the top-left corner of the new window
 */
static Point LocalGetWindowPlacement(const WindowDesc *desc, int window_number)
{
	Point pt;
	Window *w;

	if (desc->parent_cls != 0 /* WC_MAIN_WINDOW */ &&
			(w = FindWindowById(desc->parent_cls, window_number)) != NULL &&
			w->left < _screen.width - 20 && w->left > -60 && w->top < _screen.height - 20) {

		pt.x = w->left + ((desc->parent_cls == WC_BUILD_TOOLBAR || desc->parent_cls == WC_SCEN_LAND_GEN) ? 0 : 10);
		if (pt.x > _screen.width + 10 - desc->default_width) {
			pt.x = (_screen.width + 10 - desc->default_width) - 20;
		}
		pt.y = w->top + ((desc->parent_cls == WC_BUILD_TOOLBAR || desc->parent_cls == WC_SCEN_LAND_GEN) ? 36 : 10);
	} else {
		switch (desc->left) {
			case WDP_ALIGN_TBR: // Align the right side with the top toolbar
				w = FindWindowById(WC_MAIN_TOOLBAR, 0);
				pt.x = (w->left + w->width) - desc->default_width;
				break;

			case WDP_ALIGN_TBL: // Align the left side with the top toolbar
				pt.x = FindWindowById(WC_MAIN_TOOLBAR, 0)->left;
				break;

			case WDP_AUTO: // Find a good automatic position for the window
				return GetAutoPlacePosition(desc->default_width, desc->default_height);

			case WDP_CENTER: // Centre the window horizontally
				pt.x = (_screen.width - desc->default_width) / 2;
				break;

			default:
				pt.x = desc->left;
				if (pt.x < 0) pt.x += _screen.width; // negative is from right of the screen
		}

		switch (desc->top) {
			case WDP_CENTER: // Centre the window vertically
				pt.y = (_screen.height - desc->default_height) / 2;
				break;

			/* WDP_AUTO sets the position at once and is controlled by desc->left.
			 * Both left and top must be set to WDP_AUTO */
			case WDP_AUTO:
				NOT_REACHED();
				assert(desc->left == WDP_AUTO && desc->top != WDP_AUTO);
				/* fallthrough */

			default:
				pt.y = desc->top;
				if (pt.y < 0) pt.y += _screen.height; // negative is from bottom of the screen
				break;
		}
	}

	return pt;
}

/**
 * Set the positions of a new window from a WindowDesc and open it.
 *
 * @param *desc         The pointer to the WindowDesc to be created
 * @param window_number the window number of the new window
 *
 * @return Window pointer of the newly created window
 */
Window::Window(const WindowDesc *desc, WindowNumber window_number)
{
	Point pt = LocalGetWindowPlacement(desc, window_number);
	this->Initialize(pt.x, pt.y, desc->minimum_width, desc->minimum_height, desc->cls, desc->widgets, window_number);
	this->desc_flags = desc->flags;
}

/** Do a search for a window at specific coordinates. For this we start
 * at the topmost window, obviously and work our way down to the bottom
 * @param x position x to query
 * @param y position y to query
 * @return a pointer to the found window if any, NULL otherwise */
Window *FindWindowFromPt(int x, int y)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (IsInsideBS(x, w->left, w->width) && IsInsideBS(y, w->top, w->height)) {
			return w;
		}
	}

	return NULL;
}

/**
 * (re)initialize the windowing system
 */
void InitWindowSystem()
{
	IConsoleClose();

	_z_back_window = NULL;
	_z_front_window = NULL;
	_focused_window = NULL;
	_mouseover_last_w = NULL;
	_scrolling_viewport = 0;
}

/**
 * Close down the windowing system
 */
void UnInitWindowSystem()
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) delete w;

	for (w = _z_front_window; w != NULL; /* nothing */) {
		Window *to_del = w;
		w = w->z_back;
		free(to_del);
	}

	_z_front_window = NULL;
	_z_back_window = NULL;
}

/**
 * Reset the windowing system, by means of shutting it down followed by re-initialization
 */
void ResetWindowSystem()
{
	UnInitWindowSystem();
	InitWindowSystem();
	_thd.pos.x = 0;
	_thd.pos.y = 0;
	_thd.new_pos.x = 0;
	_thd.new_pos.y = 0;
}

static void DecreaseWindowCounters()
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		/* Unclick scrollbar buttons if they are pressed. */
		if (w->flags4 & (WF_SCROLL_DOWN | WF_SCROLL_UP)) {
			w->flags4 &= ~(WF_SCROLL_DOWN | WF_SCROLL_UP);
			w->SetDirty();
		}
		w->OnMouseLoop();
	}

	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->flags4 & WF_TIMEOUT_MASK && !(--w->flags4 & WF_TIMEOUT_MASK)) {
			w->OnTimeout();
			if (w->desc_flags & WDF_UNCLICK_BUTTONS) w->RaiseButtons();
		}
	}
}

Window *GetCallbackWnd()
{
	return FindWindowById(_thd.window_class, _thd.window_number);
}

static void HandlePlacePresize()
{
	if (_special_mouse_mode != WSM_PRESIZE) return;

	Window *w = GetCallbackWnd();
	if (w == NULL) return;

	Point pt = GetTileBelowCursor();
	if (pt.x == -1) {
		_thd.selend.x = -1;
		return;
	}

	w->OnPlacePresize(pt, TileVirtXY(pt.x, pt.y));
}

static bool HandleDragDrop()
{
	if (_special_mouse_mode != WSM_DRAGDROP) return true;
	if (_left_button_down) return false;

	Window *w = GetCallbackWnd();

	if (w != NULL) {
		/* send an event in client coordinates. */
		Point pt;
		pt.x = _cursor.pos.x - w->left;
		pt.y = _cursor.pos.y - w->top;
		w->OnDragDrop(pt, GetWidgetFromPos(w, pt.x, pt.y));
	}

	ResetObjectToPlace();

	return false;
}

static bool HandleMouseOver()
{
	Window *w = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);

	/* We changed window, put a MOUSEOVER event to the last window */
	if (_mouseover_last_w != NULL && _mouseover_last_w != w) {
		/* Reset mouse-over coordinates of previous window */
		Point pt = { -1, -1 };
		_mouseover_last_w->OnMouseOver(pt, 0);
	}

	/* _mouseover_last_w will get reset when the window is deleted, see DeleteWindow() */
	_mouseover_last_w = w;

	if (w != NULL) {
		/* send an event in client coordinates. */
		Point pt = { _cursor.pos.x - w->left, _cursor.pos.y - w->top };
		int widget = 0;
		if (w->widget != NULL) {
			widget = GetWidgetFromPos(w, pt.x, pt.y);
		}
		w->OnMouseOver(pt, widget);
	}

	/* Mouseover never stops execution */
	return true;
}

/**
 * Resize the window.
 * Update all the widgets of a window based on their resize flags
 * Both the areas of the old window and the new sized window are set dirty
 * ensuring proper redrawal.
 * @param w Window to resize
 * @param x delta x-size of changed window (positive if larger, etc.)
 * @param y delta y-size of changed window
 */
void ResizeWindow(Window *w, int x, int y)
{
	bool resize_height = false;
	bool resize_width = false;

	if (x == 0 && y == 0) return;

	w->SetDirty();
	for (Widget *wi = w->widget; wi->type != WWT_LAST; wi++) {
		/* Isolate the resizing flags */
		byte rsizeflag = GB(wi->display_flags, 0, 4);

		if (rsizeflag == RESIZE_NONE) continue;

		/* Resize the widget based on its resize-flag */
		if (rsizeflag & RESIZE_LEFT) {
			wi->left += x;
			resize_width = true;
		}

		if (rsizeflag & RESIZE_RIGHT) {
			wi->right += x;
			resize_width = true;
		}

		if (rsizeflag & RESIZE_TOP) {
			wi->top += y;
			resize_height = true;
		}

		if (rsizeflag & RESIZE_BOTTOM) {
			wi->bottom += y;
			resize_height = true;
		}
	}

	/* We resized at least 1 widget, so let's resize the window totally */
	if (resize_width)  w->width  += x;
	if (resize_height) w->height += y;

	w->SetDirty();
}

static bool _dragging_window; ///< A window is being dragged or resized.

static bool HandleWindowDragging()
{
	/* Get out immediately if no window is being dragged at all. */
	if (!_dragging_window) return true;

	/* Otherwise find the window... */
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->flags4 & WF_DRAGGING) {
			const Widget *t = &w->widget[1]; // the title bar ... ugh

			/* Stop the dragging if the left mouse button was released */
			if (!_left_button_down) {
				w->flags4 &= ~WF_DRAGGING;
				break;
			}

			w->SetDirty();

			int x = _cursor.pos.x + _drag_delta.x;
			int y = _cursor.pos.y + _drag_delta.y;
			int nx = x;
			int ny = y;

			if (_settings_client.gui.window_snap_radius != 0) {
				const Window *v;

				int hsnap = _settings_client.gui.window_snap_radius;
				int vsnap = _settings_client.gui.window_snap_radius;
				int delta;

				FOR_ALL_WINDOWS_FROM_BACK(v) {
					if (v == w) continue; // Don't snap at yourself

					if (y + w->height > v->top && y < v->top + v->height) {
						/* Your left border <-> other right border */
						delta = abs(v->left + v->width - x);
						if (delta <= hsnap) {
							nx = v->left + v->width;
							hsnap = delta;
						}

						/* Your right border <-> other left border */
						delta = abs(v->left - x - w->width);
						if (delta <= hsnap) {
							nx = v->left - w->width;
							hsnap = delta;
						}
					}

					if (w->top + w->height >= v->top && w->top <= v->top + v->height) {
						/* Your left border <-> other left border */
						delta = abs(v->left - x);
						if (delta <= hsnap) {
							nx = v->left;
							hsnap = delta;
						}

						/* Your right border <-> other right border */
						delta = abs(v->left + v->width - x - w->width);
						if (delta <= hsnap) {
							nx = v->left + v->width - w->width;
							hsnap = delta;
						}
					}

					if (x + w->width > v->left && x < v->left + v->width) {
						/* Your top border <-> other bottom border */
						delta = abs(v->top + v->height - y);
						if (delta <= vsnap) {
							ny = v->top + v->height;
							vsnap = delta;
						}

						/* Your bottom border <-> other top border */
						delta = abs(v->top - y - w->height);
						if (delta <= vsnap) {
							ny = v->top - w->height;
							vsnap = delta;
						}
					}

					if (w->left + w->width >= v->left && w->left <= v->left + v->width) {
						/* Your top border <-> other top border */
						delta = abs(v->top - y);
						if (delta <= vsnap) {
							ny = v->top;
							vsnap = delta;
						}

						/* Your bottom border <-> other bottom border */
						delta = abs(v->top + v->height - y - w->height);
						if (delta <= vsnap) {
							ny = v->top + v->height - w->height;
							vsnap = delta;
						}
					}
				}
			}

			/* Make sure the window doesn't leave the screen
			 * 13 is the height of the title bar */
			nx = Clamp(nx, 13 - t->right, _screen.width - 13 - t->left);
			ny = Clamp(ny, 0, _screen.height - 13);

			/* Make sure the title bar isn't hidden by behind the main tool bar */
			Window *v = FindWindowById(WC_MAIN_TOOLBAR, 0);
			if (v != NULL) {
				int v_bottom = v->top + v->height;
				int v_right = v->left + v->width;
				if (ny + t->top >= v->top && ny + t->top < v_bottom) {
					if ((v->left < 13 && nx + t->left < v->left) ||
							(v_right > _screen.width - 13 && nx + t->right > v_right)) {
						ny = v_bottom;
					} else {
						if (nx + t->left > v->left - 13 &&
								nx + t->right < v_right + 13) {
							if (w->top >= v_bottom) {
								ny = v_bottom;
							} else if (w->left < nx) {
								nx = v->left - 13 - t->left;
							} else {
								nx = v_right + 13 - t->right;
							}
						}
					}
				}
			}

			if (w->viewport != NULL) {
				w->viewport->left += nx - w->left;
				w->viewport->top  += ny - w->top;
			}
			w->left = nx;
			w->top  = ny;

			w->SetDirty();
			return false;
		} else if (w->flags4 & WF_SIZING) {
			int x, y;

			/* Stop the sizing if the left mouse button was released */
			if (!_left_button_down) {
				w->flags4 &= ~WF_SIZING;
				w->SetDirty();
				break;
			}

			x = _cursor.pos.x - _drag_delta.x;
			y = _cursor.pos.y - _drag_delta.y;

			/* X and Y has to go by step.. calculate it.
			 * The cast to int is necessary else x/y are implicitly casted to
			 * unsigned int, which won't work. */
			if (w->resize.step_width > 1) x -= x % (int)w->resize.step_width;

			if (w->resize.step_height > 1) y -= y % (int)w->resize.step_height;

			/* Check if we don't go below the minimum set size */
			if ((int)w->width + x < (int)w->resize.width)
				x = w->resize.width - w->width;
			if ((int)w->height + y < (int)w->resize.height)
				y = w->resize.height - w->height;

			/* Window already on size */
			if (x == 0 && y == 0) return false;

			/* Now find the new cursor pos.. this is NOT _cursor, because
			    we move in steps. */
			_drag_delta.x += x;
			_drag_delta.y += y;

			/* ResizeWindow sets both pre- and after-size to dirty for redrawal */
			ResizeWindow(w, x, y);

			Point size;
			Point diff;
			size.x = x + w->width;
			size.y = y + w->height;
			diff.x = x;
			diff.y = y;
			w->OnResize(size, diff);
			return false;
		}
	}

	_dragging_window = false;
	return false;
}

/**
 * Start window dragging
 * @param w Window to start dragging
 */
static void StartWindowDrag(Window *w)
{
	w->flags4 |= WF_DRAGGING;
	_dragging_window = true;

	_drag_delta.x = w->left - _cursor.pos.x;
	_drag_delta.y = w->top  - _cursor.pos.y;

	BringWindowToFront(w);
	DeleteWindowById(WC_DROPDOWN_MENU, 0);
}

/**
 * Start resizing a window
 * @param w Window to start resizing
 */
static void StartWindowSizing(Window *w)
{
	w->flags4 |= WF_SIZING;
	_dragging_window = true;

	_drag_delta.x = _cursor.pos.x;
	_drag_delta.y = _cursor.pos.y;

	BringWindowToFront(w);
	DeleteWindowById(WC_DROPDOWN_MENU, 0);
}


static bool HandleScrollbarScrolling()
{
	Window *w;

	/* Get out quickly if no item is being scrolled */
	if (!_scrolling_scrollbar) return true;

	/* Find the scrolling window */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->flags4 & WF_SCROLL_MIDDLE) {
			/* Abort if no button is clicked any more. */
			if (!_left_button_down) {
				w->flags4 &= ~WF_SCROLL_MIDDLE;
				w->SetDirty();
				break;
			}

			int i;
			Scrollbar *sb;

			if (w->flags4 & WF_HSCROLL) {
				sb = &w->hscroll;
				i = _cursor.pos.x - _cursorpos_drag_start.x;
			} else if (w->flags4 & WF_SCROLL2){
				sb = &w->vscroll2;
				i = _cursor.pos.y - _cursorpos_drag_start.y;
			} else {
				sb = &w->vscroll;
				i = _cursor.pos.y - _cursorpos_drag_start.y;
			}

			/* Find the item we want to move to and make sure it's inside bounds. */
			int pos = min(max(0, i + _scrollbar_start_pos) * sb->count / _scrollbar_size, max(0, sb->count - sb->cap));
			if (pos != sb->pos) {
				sb->pos = pos;
				w->SetDirty();
			}
			return false;
		}
	}

	_scrolling_scrollbar = false;
	return false;
}

static bool HandleViewportScroll()
{
	bool scrollwheel_scrolling = _settings_client.gui.scrollwheel_scrolling == 1 && (_cursor.v_wheel != 0 || _cursor.h_wheel != 0);

	if (!_scrolling_viewport) return true;

	Window *w = FindWindowFromPt(_cursor.pos.x, _cursor.pos.y);

	if (!(_right_button_down || scrollwheel_scrolling || (_settings_client.gui.left_mouse_btn_scrolling && _left_button_down)) || w == NULL) {
		_cursor.fix_at = false;
		_scrolling_viewport = false;
		return true;
	}

	if (w == FindWindowById(WC_MAIN_WINDOW, 0) && w->viewport->follow_vehicle != INVALID_VEHICLE) {
		/* If the main window is following a vehicle, then first let go of it! */
		const Vehicle *veh = GetVehicle(w->viewport->follow_vehicle);
		ScrollMainWindowTo(veh->x_pos, veh->y_pos, veh->z_pos, true); // This also resets follow_vehicle
		return true;
	}

	Point delta;
	if (_settings_client.gui.reverse_scroll || (_settings_client.gui.left_mouse_btn_scrolling && _left_button_down)) {
		delta.x = -_cursor.delta.x;
		delta.y = -_cursor.delta.y;
	} else {
		delta.x = _cursor.delta.x;
		delta.y = _cursor.delta.y;
	}

	if (scrollwheel_scrolling) {
		/* We are using scrollwheels for scrolling */
		delta.x = _cursor.h_wheel;
		delta.y = _cursor.v_wheel;
		_cursor.v_wheel = 0;
		_cursor.h_wheel = 0;
	}

	/* Create a scroll-event and send it to the window */
	w->OnScroll(delta);

	_cursor.delta.x = 0;
	_cursor.delta.y = 0;
	return false;
}

/** Check if a window can be made top-most window, and if so do
 * it. If a window does not obscure any other windows, it will not
 * be brought to the foreground. Also if the only obscuring windows
 * are so-called system-windows, the window will not be moved.
 * The function will return false when a child window of this window is a
 * modal-popup; function returns a false and child window gets a white border
 * @param w Window to bring on-top
 * @return false if the window has an active modal child, true otherwise */
static bool MaybeBringWindowToFront(Window *w)
{
	bool bring_to_front = false;

	if (w->window_class == WC_MAIN_WINDOW ||
			IsVitalWindow(w) ||
			w->window_class == WC_TOOLTIPS ||
			w->window_class == WC_DROPDOWN_MENU) {
		return true;
	}

	Window *u;
	FOR_ALL_WINDOWS_FROM_BACK_FROM(u, w->z_front) {
		/* A modal child will prevent the activation of the parent window */
		if (u->parent == w && (u->desc_flags & WDF_MODAL)) {
			u->flags4 |= WF_WHITE_BORDER_MASK;
			u->SetDirty();
			return false;
		}

		if (u->window_class == WC_MAIN_WINDOW ||
				IsVitalWindow(u) ||
				u->window_class == WC_TOOLTIPS ||
				u->window_class == WC_DROPDOWN_MENU) {
			continue;
		}

		/* Window sizes don't interfere, leave z-order alone */
		if (w->left + w->width <= u->left ||
				u->left + u->width <= w->left ||
				w->top  + w->height <= u->top ||
				u->top + u->height <= w->top) {
			continue;
		}

		bring_to_front = true;
	}

	if (bring_to_front) BringWindowToFront(w);
	return true;
}

/** Handle keyboard input.
 * @param raw_key Lower 8 bits contain the ASCII character, the higher 16 bits the keycode
 */
void HandleKeypress(uint32 raw_key)
{
	/*
	 * During the generation of the world, there might be
	 * another thread that is currently building for example
	 * a road. To not interfere with those tasks, we should
	 * NOT change the _current_company here.
	 *
	 * This is not necessary either, as the only events that
	 * can be handled are the 'close application' events
	 */
	if (!IsGeneratingWorld()) _current_company = _local_company;

	/* Setup event */
	uint16 key     = GB(raw_key,  0, 16);
	uint16 keycode = GB(raw_key, 16, 16);

	/*
	 * The Unicode standard defines an area called the private use area. Code points in this
	 * area are reserved for private use and thus not portable between systems. For instance,
	 * Apple defines code points for the arrow keys in this area, but these are only printable
	 * on a system running OS X. We don't want these keys to show up in text fields and such,
	 * and thus we have to clear the unicode character when we encounter such a key.
	 */
	if (key >= 0xE000 && key <= 0xF8FF) key = 0;

	/*
	 * If both key and keycode is zero, we don't bother to process the event.
	 */
	if (key == 0 && keycode == 0) return;

	/* Check if the focused window has a focused editbox */
	if (EditBoxInGlobalFocus()) {
		/* All input will in this case go to the focused window */
		if (_focused_window->OnKeyPress(key, keycode) == Window::ES_HANDLED) return;
	}

	/* Call the event, start with the uppermost window. */
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->OnKeyPress(key, keycode) == Window::ES_HANDLED) return;
	}

	w = FindWindowById(WC_MAIN_TOOLBAR, 0);
	/* When there is no toolbar w is null, check for that */
	if (w != NULL) w->OnKeyPress(key, keycode);
}

/**
 * State of CONTROL key has changed
 */
void HandleCtrlChanged()
{
	/* Call the event, start with the uppermost window. */
	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->OnCTRLStateChange() == Window::ES_HANDLED) return;
	}
}

/**
 * Local counter that is incremented each time an mouse input event is detected.
 * The counter is used to stop auto-scrolling.
 * @see HandleAutoscroll()
 * @see HandleMouseEvents()
 */
static int _input_events_this_tick = 0;

/**
 * If needed and switched on, perform auto scrolling (automatically
 * moving window contents when mouse is near edge of the window).
 */
static void HandleAutoscroll()
{
	if (_settings_client.gui.autoscroll && _game_mode != GM_MENU && !IsGeneratingWorld()) {
		int x = _cursor.pos.x;
		int y = _cursor.pos.y;
		Window *w = FindWindowFromPt(x, y);
		if (w == NULL || w->flags4 & WF_DISABLE_VP_SCROLL) return;
		ViewPort *vp = IsPtInWindowViewport(w, x, y);
		if (vp != NULL) {
			x -= vp->left;
			y -= vp->top;

			/* here allows scrolling in both x and y axis */
#define scrollspeed 3
			if (x - 15 < 0) {
				w->viewport->dest_scrollpos_x += ScaleByZoom((x - 15) * scrollspeed, vp->zoom);
			} else if (15 - (vp->width - x) > 0) {
				w->viewport->dest_scrollpos_x += ScaleByZoom((15 - (vp->width - x)) * scrollspeed, vp->zoom);
			}
			if (y - 15 < 0) {
				w->viewport->dest_scrollpos_y += ScaleByZoom((y - 15) * scrollspeed, vp->zoom);
			} else if (15 - (vp->height - y) > 0) {
				w->viewport->dest_scrollpos_y += ScaleByZoom((15 - (vp->height - y)) * scrollspeed, vp->zoom);
			}
#undef scrollspeed
		}
	}
}

enum MouseClick {
	MC_NONE = 0,
	MC_LEFT,
	MC_RIGHT,
	MC_DOUBLE_LEFT,

	MAX_OFFSET_DOUBLE_CLICK = 5,     ///< How much the mouse is allowed to move to call it a double click
	TIME_BETWEEN_DOUBLE_CLICK = 500, ///< Time between 2 left clicks before it becoming a double click, in ms
};

extern bool VpHandlePlaceSizingDrag();

static void ScrollMainViewport(int x, int y)
{
	if (_game_mode != GM_MENU) {
		Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
		assert(w);

		w->viewport->dest_scrollpos_x += ScaleByZoom(x, w->viewport->zoom);
		w->viewport->dest_scrollpos_y += ScaleByZoom(y, w->viewport->zoom);
	}
}

/**
 * Describes all the different arrow key combinations the game allows
 * when it is in scrolling mode.
 * The real arrow keys are bitwise numbered as
 * 1 = left
 * 2 = up
 * 4 = right
 * 8 = down
 */
static const int8 scrollamt[16][2] = {
	{ 0,  0}, ///<  no key specified
	{-2,  0}, ///<  1 : left
	{ 0, -2}, ///<  2 : up
	{-2, -1}, ///<  3 : left  + up
	{ 2,  0}, ///<  4 : right
	{ 0,  0}, ///<  5 : left  + right = nothing
	{ 2, -1}, ///<  6 : right + up
	{ 0, -2}, ///<  7 : right + left  + up = up
	{ 0  ,2}, ///<  8 : down
	{-2  ,1}, ///<  9 : down  + left
	{ 0,  0}, ///< 10 : down  + up    = nothing
	{-2,  0}, ///< 11 : left  + up    +  down = left
	{ 2,  1}, ///< 12 : down  + right
	{ 0,  2}, ///< 13 : left  + right +  down = down
	{ 2,  0}, ///< 14 : right + up    +  down = right
	{ 0,  0}, ///< 15 : left  + up    +  right + down  = nothing
};

static void HandleKeyScrolling()
{
	/*
	 * Check that any of the dirkeys is pressed and that the focused window
	 * dont has an edit-box as focused widget.
	 */
	if (_dirkeys && !EditBoxInGlobalFocus()) {
		int factor = _shift_pressed ? 50 : 10;
		ScrollMainViewport(scrollamt[_dirkeys][0] * factor, scrollamt[_dirkeys][1] * factor);
	}
}

void MouseLoop(MouseClick click, int mousewheel)
{
	DecreaseWindowCounters();
	HandlePlacePresize();
	UpdateTileSelection();

	if (!VpHandlePlaceSizingDrag())  return;
	if (!HandleDragDrop())           return;
	if (!HandleWindowDragging())     return;
	if (!HandleScrollbarScrolling()) return;
	if (!HandleViewportScroll())     return;
	if (!HandleMouseOver())          return;

	bool scrollwheel_scrolling = _settings_client.gui.scrollwheel_scrolling == 1 && (_cursor.v_wheel != 0 || _cursor.h_wheel != 0);
	if (click == MC_NONE && mousewheel == 0 && !scrollwheel_scrolling) return;

	int x = _cursor.pos.x;
	int y = _cursor.pos.y;
	Window *w = FindWindowFromPt(x, y);
	if (w == NULL) return;

	if (!MaybeBringWindowToFront(w)) return;
	ViewPort *vp = IsPtInWindowViewport(w, x, y);

	/* Don't allow any action in a viewport if either in menu of in generating world */
	if (vp != NULL && (_game_mode == GM_MENU || IsGeneratingWorld())) return;

	if (mousewheel != 0) {
		if (_settings_client.gui.scrollwheel_scrolling == 0) {
			/* Send mousewheel event to window */
			w->OnMouseWheel(mousewheel);
		}

		/* Dispatch a MouseWheelEvent for widgets if it is not a viewport */
		if (vp == NULL) DispatchMouseWheelEvent(w, GetWidgetFromPos(w, x - w->left, y - w->top), mousewheel);
	}

	if (vp != NULL) {
		if (scrollwheel_scrolling) click = MC_RIGHT; // we are using the scrollwheel in a viewport, so we emulate right mouse button
		switch (click) {
			case MC_DOUBLE_LEFT:
			case MC_LEFT:
				DEBUG(misc, 2, "Cursor: 0x%X (%d)", _cursor.sprite, _cursor.sprite);
				if (_thd.place_mode != VHM_NONE &&
						/* query button and place sign button work in pause mode */
						_cursor.sprite != SPR_CURSOR_QUERY &&
						_cursor.sprite != SPR_CURSOR_SIGN &&
						_pause_game != 0 &&
						!_cheats.build_in_pause.value) {
					return;
				}

				if (_thd.place_mode == VHM_NONE) {
					if (!HandleViewportClicked(vp, x, y) &&
							!(w->flags4 & WF_DISABLE_VP_SCROLL) &&
							_settings_client.gui.left_mouse_btn_scrolling) {
						_scrolling_viewport = true;
						_cursor.fix_at = false;
					}
				} else {
					PlaceObject();
				}
				break;

			case MC_RIGHT:
				if (!(w->flags4 & WF_DISABLE_VP_SCROLL)) {
					_scrolling_viewport = true;
					_cursor.fix_at = true;
				}
				break;

			default:
				break;
		}
	} else {
		switch (click) {
			case MC_DOUBLE_LEFT:
				DispatchLeftClickEvent(w, x - w->left, y - w->top, true);
				if (_mouseover_last_w == NULL) break; // The window got removed.
				/* fallthough, and also give a single-click for backwards compatibility */
			case MC_LEFT:
				DispatchLeftClickEvent(w, x - w->left, y - w->top, false);
				break;

			default:
				if (!scrollwheel_scrolling || w == NULL || w->window_class != WC_SMALLMAP) break;
				/* We try to use the scrollwheel to scroll since we didn't touch any of the buttons.
				 * Simulate a right button click so we can get started. */

				/* fallthough */
			case MC_RIGHT: DispatchRightClickEvent(w, x - w->left, y - w->top); break;
		}
	}
}

/**
 * Handle a mouse event from the video driver
 */
void HandleMouseEvents()
{
	static int double_click_time = 0;
	static int double_click_x = 0;
	static int double_click_y = 0;

	/*
	 * During the generation of the world, there might be
	 * another thread that is currently building for example
	 * a road. To not interfere with those tasks, we should
	 * NOT change the _current_company here.
	 *
	 * This is not necessary either, as the only events that
	 * can be handled are the 'close application' events
	 */
	if (!IsGeneratingWorld()) _current_company = _local_company;

	/* Mouse event? */
	MouseClick click = MC_NONE;
	if (_left_button_down && !_left_button_clicked) {
		click = MC_LEFT;
		if (double_click_time != 0 && _realtime_tick - double_click_time   < TIME_BETWEEN_DOUBLE_CLICK &&
			  double_click_x != 0    && abs(_cursor.pos.x - double_click_x) < MAX_OFFSET_DOUBLE_CLICK  &&
			  double_click_y != 0    && abs(_cursor.pos.y - double_click_y) < MAX_OFFSET_DOUBLE_CLICK) {
			click = MC_DOUBLE_LEFT;
		}
		double_click_time = _realtime_tick;
		double_click_x = _cursor.pos.x;
		double_click_y = _cursor.pos.y;
		_left_button_clicked = true;
		_input_events_this_tick++;
	} else if (_right_button_clicked) {
		_right_button_clicked = false;
		click = MC_RIGHT;
		_input_events_this_tick++;
	}

	int mousewheel = 0;
	if (_cursor.wheel) {
		mousewheel = _cursor.wheel;
		_cursor.wheel = 0;
		_input_events_this_tick++;
	}

	MouseLoop(click, mousewheel);
}

/**
 * Check the soft limit of deletable (non vital, non sticky) windows.
 */
static void CheckSoftLimit()
{
	if (_settings_client.gui.window_soft_limit == 0) return;

	for (;;) {
		uint deletable_count = 0;
		Window *w, *last_deletable = NULL;
		FOR_ALL_WINDOWS_FROM_FRONT(w) {
			if (w->window_class == WC_MAIN_WINDOW || IsVitalWindow(w) || (w->flags4 & WF_STICKY)) continue;

			last_deletable = w;
			deletable_count++;
		}

		/* We've ot reached the soft limit yet */
		if (deletable_count <= _settings_client.gui.window_soft_limit) break;

		assert(last_deletable != NULL);
		delete last_deletable;
	}
}

/**
 * Regular call from the global game loop
 */
void InputLoop()
{
	CheckSoftLimit();
	HandleKeyScrolling();

	/* Do the actual free of the deleted windows. */
	for (Window *v = _z_front_window; v != NULL; /* nothing */) {
		Window *w = v;
		v = v->z_back;

		if (w->window_class != WC_INVALID) continue;

		/* Find the window in the z-array, and effectively remove it
		 * by moving all windows after it one to the left. This must be
		 * done before removing the child so we cannot cause recursion
		 * between the deletion of the parent and the child. */
		if (w->z_front == NULL) {
			_z_front_window = w->z_back;
		} else {
			w->z_front->z_back = w->z_back;
		}
		if (w->z_back == NULL) {
			_z_back_window  = w->z_front;
		} else {
			w->z_back->z_front = w->z_front;
		}
		free(w);
	}

	if (_input_events_this_tick != 0) {
		/* The input loop is called only once per GameLoop() - so we can clear the counter here */
		_input_events_this_tick = 0;
		/* there were some inputs this tick, don't scroll ??? */
		return;
	}

	/* HandleMouseEvents was already called for this tick */
	HandleMouseEvents();
	HandleAutoscroll();
}

/**
 * Update the continuously changing contents of the windows, such as the viewports
 */
void UpdateWindows()
{
	Window *w;
	static int we4_timer = 0;
	int t = we4_timer + 1;

	if (t >= 100) {
		FOR_ALL_WINDOWS_FROM_FRONT(w) {
			w->OnHundredthTick();
		}
		t = 0;
	}
	we4_timer = t;

	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		if (w->flags4 & WF_WHITE_BORDER_MASK) {
			w->flags4 -= WF_WHITE_BORDER_ONE;

			if (!(w->flags4 & WF_WHITE_BORDER_MASK)) w->SetDirty();
		}
	}

	DrawDirtyBlocks();

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->viewport != NULL) UpdateViewportPosition(w);
	}
	NetworkDrawChatMessage();
	/* Redraw mouse cursor in case it was hidden */
	DrawMouseCursor();
}

/**
 * Mark window as dirty (in need of repainting)
 * @param cls Window class
 * @param number Window number in that class
 */
void InvalidateWindow(WindowClass cls, WindowNumber number)
{
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) w->SetDirty();
	}
}

/**
 * Mark a particular widget in a particular window as dirty (in need of repainting)
 * @param cls Window class
 * @param number Window number in that class
 * @param widget_index Index number of the widget that needs repainting
 */
void InvalidateWindowWidget(WindowClass cls, WindowNumber number, byte widget_index)
{
	const Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) {
			w->InvalidateWidget(widget_index);
		}
	}
}

/**
 * Mark all windows of a particular class as dirty (in need of repainting)
 * @param cls Window class
 */
void InvalidateWindowClasses(WindowClass cls)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) w->SetDirty();
	}
}

/**
 * Mark window data as invalid (in need of re-computing)
 * @param w Window with invalid data
 */
void InvalidateThisWindowData(Window *w, int data)
{
	w->OnInvalidateData(data);
	w->SetDirty();
}

/**
 * Mark window data of the window of a given class and specific window number as invalid (in need of re-computing)
 * @param cls Window class
 * @param number Window number within the class
 */
void InvalidateWindowData(WindowClass cls, WindowNumber number, int data)
{
	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls && w->window_number == number) InvalidateThisWindowData(w, data);
	}
}

/**
 * Mark window data of all windows of a given class as invalid (in need of re-computing)
 * @param cls Window class
 */
void InvalidateWindowClassesData(WindowClass cls, int data)
{
	Window *w;

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class == cls) InvalidateThisWindowData(w, data);
	}
}

/**
 * Dispatch WE_TICK event over all windows
 */
void CallWindowTickEvent()
{
	if (_scroller_click_timeout > 3) {
		_scroller_click_timeout -= 3;
	} else {
		_scroller_click_timeout = 0;
	}

	Window *w;
	FOR_ALL_WINDOWS_FROM_FRONT(w) {
		w->OnTick();
	}
}

/**
 * Try to delete a non-vital window.
 * Non-vital windows are windows other than the game selection, main toolbar,
 * status bar, toolbar menu, and tooltip windows. Stickied windows are also
 * considered vital.
 */
void DeleteNonVitalWindows()
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->window_class != WC_MAIN_WINDOW &&
				w->window_class != WC_SELECT_GAME &&
				w->window_class != WC_MAIN_TOOLBAR &&
				w->window_class != WC_STATUS_BAR &&
				w->window_class != WC_TOOLBAR_MENU &&
				w->window_class != WC_TOOLTIPS &&
				(w->flags4 & WF_STICKY) == 0) { // do not delete windows which are 'pinned'

			delete w;
			goto restart_search;
		}
	}
}

/** It is possible that a stickied window gets to a position where the
 * 'close' button is outside the gaming area. You cannot close it then; except
 * with this function. It closes all windows calling the standard function,
 * then, does a little hacked loop of closing all stickied windows. Note
 * that standard windows (status bar, etc.) are not stickied, so these aren't affected */
void DeleteAllNonVitalWindows()
{
	Window *w;

	/* Delete every window except for stickied ones, then sticky ones as well */
	DeleteNonVitalWindows();

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->flags4 & WF_STICKY) {
			delete w;
			goto restart_search;
		}
	}
}

/**
 * Delete all windows that are used for construction of vehicle etc.
 * Once done with that invalidate the others to ensure they get refreshed too.
 */
void DeleteConstructionWindows()
{
	Window *w;

restart_search:
	/* When we find the window to delete, we need to restart the search
	 * as deleting this window could cascade in deleting (many) others
	 * anywhere in the z-array */
	FOR_ALL_WINDOWS_FROM_BACK(w) {
		if (w->desc_flags & WDF_CONSTRUCTION) {
			delete w;
			goto restart_search;
		}
	}

	FOR_ALL_WINDOWS_FROM_BACK(w) w->SetDirty();
}

/** Delete all always on-top windows to get an empty screen */
void HideVitalWindows()
{
	DeleteWindowById(WC_TOOLBAR_MENU, 0);
	DeleteWindowById(WC_MAIN_TOOLBAR, 0);
	DeleteWindowById(WC_STATUS_BAR, 0);
}

/**
 * (Re)position main toolbar window at the screen
 * @param w Window structure of the main toolbar window, may also be \c NULL
 * @return X coordinate of left edge of the repositioned toolbar window
 */
int PositionMainToolbar(Window *w)
{
	DEBUG(misc, 5, "Repositioning Main Toolbar...");

	if (w == NULL || w->window_class != WC_MAIN_TOOLBAR) {
		w = FindWindowById(WC_MAIN_TOOLBAR, 0);
	}

	switch (_settings_client.gui.toolbar_pos) {
		case 1:  w->left = (_screen.width - w->width) / 2; break;
		case 2:  w->left = _screen.width - w->width; break;
		default: w->left = 0;
	}
	SetDirtyBlocks(0, 0, _screen.width, w->height); // invalidate the whole top part
	return w->left;
}

/**
 * Set the number of items of the vertical scrollbar.
 *
 * Function also updates the position of the scrollbar if necessary.
 * @param w   Window containing the vertical scrollbar
 * @param num New number of items
 */
void SetVScrollCount(Window *w, int num)
{
	w->vscroll.count = num;
	num -= w->vscroll.cap;
	if (num < 0) num = 0;
	if (num < w->vscroll.pos) w->vscroll.pos = num;
}

/**
 * Set the number of items of the second vertical scrollbar.
 *
 * Function also updates the position of the scrollbar if necessary.
 * @param w   Window containing the second vertical scrollbar
 * @param num New number of items
 */
void SetVScroll2Count(Window *w, int num)
{
	w->vscroll2.count = num;
	num -= w->vscroll2.cap;
	if (num < 0) num = 0;
	if (num < w->vscroll2.pos) w->vscroll2.pos = num;
}

/**
 * Set the number of items of the horizontal scrollbar.
 *
 * Function also updates the position of the scrollbar if necessary.
 * @param w   Window containing the horizontal scrollbar
 * @param num New number of items
 */
void SetHScrollCount(Window *w, int num)
{
	w->hscroll.count = num;
	num -= w->hscroll.cap;
	if (num < 0) num = 0;
	if (num < w->hscroll.pos) w->hscroll.pos = num;
}

/**
 * Relocate all windows to fit the new size of the game application screen
 * @param neww New width of the game application screen
 * @param newh New height of the game appliction screen
 */
void RelocateAllWindows(int neww, int newh)
{
	Window *w;

	FOR_ALL_WINDOWS_FROM_BACK(w) {
		int left, top;

		if (w->window_class == WC_MAIN_WINDOW) {
			ViewPort *vp = w->viewport;
			vp->width = w->width = neww;
			vp->height = w->height = newh;
			vp->virtual_width = ScaleByZoom(neww, vp->zoom);
			vp->virtual_height = ScaleByZoom(newh, vp->zoom);
			continue; // don't modify top,left
		}

		/* XXX - this probably needs something more sane. For example specying
		 * in a 'backup'-desc that the window should always be centred. */
		switch (w->window_class) {
			case WC_MAIN_TOOLBAR:
				if (neww - w->width != 0) {
					ResizeWindow(w, min(neww, 640) - w->width, 0);

					Point size;
					Point delta;
					size.x = w->width;
					size.y = w->height;
					delta.x = neww - w->width;
					delta.y = 0;
					w->OnResize(size, delta);
				}

				top = w->top;
				left = PositionMainToolbar(w); // changes toolbar orientation
				break;

			case WC_SELECT_GAME:
			case WC_GAME_OPTIONS:
			case WC_NETWORK_WINDOW:
				top = (newh - w->height) >> 1;
				left = (neww - w->width) >> 1;
				break;

			case WC_NEWS_WINDOW:
				top = newh - w->height;
				left = (neww - w->width) >> 1;
				break;

			case WC_STATUS_BAR:
				ResizeWindow(w, Clamp(neww, 320, 640) - w->width, 0);
				top = newh - w->height;
				left = (neww - w->width) >> 1;
				break;

			case WC_SEND_NETWORK_MSG:
				ResizeWindow(w, Clamp(neww, 320, 640) - w->width, 0);
				top = (newh - 26); // 26 = height of status bar + height of chat bar
				left = (neww - w->width) >> 1;
				break;

			case WC_CONSOLE:
				IConsoleResize(w);
				continue;

			default: {
				left = w->left;
				if (left + (w->width >> 1) >= neww) left = neww - w->width;
				if (left < 0) left = 0;

				top = w->top;
				if (top + (w->height >> 1) >= newh) top = newh - w->height;

				const Window *wt = FindWindowById(WC_MAIN_TOOLBAR, 0);
				if (wt != NULL) {
					if (top < wt->height && wt->left < (w->left + w->width) && (wt->left + wt->width) > w->left) top = wt->height;
					if (top >= newh) top = newh - 1;
				} else {
					if (top < 0) top = 0;
				}
			} break;
		}

		if (w->viewport != NULL) {
			w->viewport->left += left - w->left;
			w->viewport->top += top - w->top;
		}

		w->left = left;
		w->top = top;
	}
}

/** Destructor of the base class PickerWindowBase
 * Main utility is to stop the base Window destructor from triggering
 * a free while the child will already be free, in this case by the ResetObjectToPlace().
 */
PickerWindowBase::~PickerWindowBase()
{
	this->window_class = WC_INVALID; // stop the ancestor from freeing the already (to be) child
	ResetObjectToPlace();
}
