#include "app/X11.h"

#include <QGuiApplication>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <xcb/xcb.h>

namespace qr::x11
{

namespace
{
template <typename T>
using XcbPtr = std::unique_ptr<T, decltype(&std::free)>;

template <typename T>
XcbPtr<T> wrap(T* p)
{
    return XcbPtr<T>(p, &std::free);
}

xcb_atom_t atom(const char* name)
{
    xcb_connection_t* c = connection();
    auto reply = wrap(xcb_intern_atom_reply(c, xcb_intern_atom(c, 1, uint16_t(std::strlen(name)), name), nullptr));
    return reply ? reply->atom : XCB_ATOM_NONE;
}

// Reads a 32-bit-format property (CARDINAL / WINDOW / ATOM lists).
std::vector<uint32_t> property32(xcb_window_t w, xcb_atom_t prop, xcb_atom_t type)
{
    if(prop == XCB_ATOM_NONE)
        return {};
    xcb_connection_t* c = connection();
    auto reply = wrap(xcb_get_property_reply(c, xcb_get_property(c, 0, w, prop, type, 0, 4096), nullptr));
    if(!reply || reply->format != 32)
        return {};
    const auto* data = static_cast<const uint32_t*>(xcb_get_property_value(reply.get()));
    const int n = xcb_get_property_value_length(reply.get()) / 4;
    return std::vector<uint32_t>(data, data + n);
}

bool contains(const std::vector<uint32_t>& v, uint32_t x)
{
    for(uint32_t e : v)
        if(e == x)
            return true;
    return false;
}
} // namespace

xcb_connection_t* connection()
{
    auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

uint32_t rootWindow()
{
    xcb_connection_t* c = connection();
    if(!c)
        return 0;
    // Qt opens the default screen; the root of screen 0 is what x11grab ":N" captures.
    return xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
}

QString displayName()
{
    const QString d = qEnvironmentVariable("DISPLAY");
    return d.isEmpty() ? QStringLiteral(":0") : d;
}

QRect rootGeometry()
{
    xcb_connection_t* c = connection();
    if(!c)
        return {};
    const xcb_screen_t* s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    return {0, 0, s->width_in_pixels, s->height_in_pixels};
}

QImage grabRoot(const QRect& physical)
{
    xcb_connection_t* c = connection();
    const QRect r = physical.intersected(rootGeometry());
    if(!c || r.isEmpty())
        return {};
    const xcb_setup_t* setup = xcb_get_setup(c);
    const xcb_screen_t* screen = xcb_setup_roots_iterator(setup).data;

    // Only the layout every desktop X server uses: 24/32-bit TrueColor, 32 bits per pixel,
    // little-endian — which is exactly QImage::Format_RGB32.
    int bpp = 0;
    for(auto it = xcb_setup_pixmap_formats_iterator(setup); it.rem; xcb_format_next(&it))
        if(it.data->depth == screen->root_depth)
            bpp = it.data->bits_per_pixel;
    if(bpp != 32 || screen->root_depth < 24 || setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST)
        return {};

    auto reply = wrap(xcb_get_image_reply(c,
                                          xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root, int16_t(r.x()),
                                                        int16_t(r.y()), uint16_t(r.width()), uint16_t(r.height()),
                                                        ~0u),
                                          nullptr));
    if(!reply)
        return {};
    const int len = xcb_get_image_data_length(reply.get());
    const qsizetype stride = qsizetype(r.width()) * 4;
    if(len < stride * r.height())
        return {};
    // Copy out of the reply buffer, which is freed when `reply` goes out of scope.
    return QImage(xcb_get_image_data(reply.get()), r.width(), r.height(), stride, QImage::Format_RGB32).copy();
}

QList<QRect> visibleWindowRects()
{
    QList<QRect> out;
    xcb_connection_t* c = connection();
    if(!c)
        return out;
    const xcb_window_t root = rootWindow();

    const xcb_atom_t clientList = atom("_NET_CLIENT_LIST_STACKING");
    const xcb_atom_t currentDesktopAtom = atom("_NET_CURRENT_DESKTOP");
    const xcb_atom_t wmDesktop = atom("_NET_WM_DESKTOP");
    const xcb_atom_t wmState = atom("_NET_WM_STATE");
    const xcb_atom_t stateHidden = atom("_NET_WM_STATE_HIDDEN");
    const xcb_atom_t wmType = atom("_NET_WM_WINDOW_TYPE");
    const xcb_atom_t typeDesktop = atom("_NET_WM_WINDOW_TYPE_DESKTOP");
    const xcb_atom_t frameExtents = atom("_NET_FRAME_EXTENTS");
    const xcb_atom_t gtkFrameExtents = atom("_GTK_FRAME_EXTENTS");

    const auto current = property32(root, currentDesktopAtom, XCB_ATOM_CARDINAL);
    const auto windows = property32(root, clientList, XCB_ATOM_WINDOW); // bottom → top

    for(auto it = windows.rbegin(); it != windows.rend(); ++it)
    {
        const xcb_window_t w = *it;

        auto attrs = wrap(xcb_get_window_attributes_reply(c, xcb_get_window_attributes(c, w), nullptr));
        if(!attrs || attrs->map_state != XCB_MAP_STATE_VIEWABLE)
            continue;

        const auto desktop = property32(w, wmDesktop, XCB_ATOM_CARDINAL);
        if(!desktop.empty() && !current.empty() && desktop[0] != 0xFFFFFFFF && desktop[0] != current[0])
            continue;
        if(contains(property32(w, wmState, XCB_ATOM_ATOM), stateHidden))
            continue;
        if(contains(property32(w, wmType, XCB_ATOM_ATOM), typeDesktop))
            continue;

        auto geo = wrap(xcb_get_geometry_reply(c, xcb_get_geometry(c, w), nullptr));
        auto pos = wrap(xcb_translate_coordinates_reply(c, xcb_translate_coordinates(c, w, root, 0, 0), nullptr));
        if(!geo || !pos)
            continue;
        QRect r(pos->dst_x, pos->dst_y, geo->width, geo->height);

        if(const auto fe = property32(w, frameExtents, XCB_ATOM_CARDINAL); fe.size() == 4)
            r.adjust(-int(fe[0]), -int(fe[2]), int(fe[1]), int(fe[3])); // left, right, top, bottom
        if(const auto ge = property32(w, gtkFrameExtents, XCB_ATOM_CARDINAL); ge.size() == 4)
            r.adjust(int(ge[0]), int(ge[2]), -int(ge[1]), -int(ge[3]));

        if(r.width() > 1 && r.height() > 1)
            out << r;
    }
    return out;
}

} // namespace qr::x11
