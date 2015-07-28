%bcond_without cairo_xcb_backend 
%bcond_without cairo_gl_backend
%bcond_with wayland
%bcond_with x
%bcond_with desktop


Name:           cairo
#Version:        1.12.16
Version:        1.14.2
Release:        0
License:        LGPL-2.1+ or MPL-1.1
Summary:        Vector Graphics Library with Cross-Device Output Support
Url:            http://cairographics.org/
Group:          Graphics/Libraries
Source:         http://cairographics.org/releases/%{name}-%{version}.tar.xz
Source99:       baselibs.conf
Source1001: 	cairo.manifest
#BuildRequires:  libtool
BuildRequires:  pkg-config
BuildRequires:  xz
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(gobject-2.0)
BuildRequires:  pkgconfig(libpng)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  which
BuildRequires:	pkgconfig(ecore)
BuildRequires:	pkgconfig(evas)
%if %{with cairo_gl_backend}
%if %{with x} && %{with desktop}
BuildRequires:  pkgconfig(gl)
%endif
BuildRequires:  pkgconfig(glesv2)
%if %{with wayland}
BuildRequires:	pkgconfig(wayland-egl)
%endif
%endif
%if %{with x}
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xrender)
%if %{with cairo_xcb_backend}
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xcb-shm)
%endif
%endif


%description
Cairo is a vector graphics library with cross-device output support.
Currently supported output targets include the X Window System,
in-memory image buffers, and PostScript. Cairo is designed to produce
identical output on all output media while taking advantage of display
hardware acceleration when available.

%package -n libcairo
License:        LGPL-2.1+ or MPL-1.1
Summary:        Vector Graphics Library with Cross-Device Output Support
Group:          Graphics/Libraries
Provides:       cairo = %{version}
#Obsoletes:      cairo < %{version}

%description -n libcairo
Cairo is a vector graphics library with cross-device output support.
Currently supported output targets include the X Window System,
in-memory image buffers, and PostScript. Cairo is designed to produce
identical output on all output media while taking advantage of display
hardware acceleration when available.

%package -n libcairo-gobject
License:        LGPL-2.1+ or MPL-1.1
Summary:        Vector Graphics Library with Cross-Device Output Support
Group:          Graphics/Libraries

%description -n libcairo-gobject
Cairo is a vector graphics library with cross-device output support.
Currently supported output targets include the X Window System,
in-memory image buffers, and PostScript. Cairo is designed to produce
identical output on all output media while taking advantage of display
hardware acceleration when available.

This library contains GType declarations for Cairo types. It is also
meant to support gobject-introspection binding creation.

%package -n libcairo-script-interpreter
License:        LGPL-2.1+ or MPL-1.1
Summary:        Vector Graphics Library with Cross-Device Output Support
Group:          Graphics/Libraries

%description -n libcairo-script-interpreter
Cairo is a vector graphics library with cross-device output support.
Currently supported output targets include the X Window System,
in-memory image buffers, and PostScript. Cairo is designed to produce
identical output on all output media while taking advantage of display
hardware acceleration when available.

%package -n tools
License:        GPL-3.0+
Summary:        Vector Graphics Library with Cross-Device Output Support -- Utilities
Group:          Development/Libraries
# We need an explicit requires since nothing links to the cairo library
#Requires:       libcairo = %{version}

%description -n tools
Cairo is a vector graphics library with cross-device output support.
Currently supported output targets include the X Window System,
in-memory image buffers, and PostScript. Cairo is designed to produce
identical output on all output media while taking advantage of display
hardware acceleration when available.

This package contains various cairo utilities.

%package devel
License:        LGPL-2.1+ or MPL-1.1
Summary:        Development environment for cairo
Group:          Development/Libraries
Requires:       libcairo = %{version}
Requires:       libcairo-gobject = %{version}
Requires:       libcairo-script-interpreter = %{version}
Requires:       pixman-devel

%description devel
This package contains all files necessary to build binaries using
cairo.

%prep
%setup -q
cp %{SOURCE1001} .

%build
# Disable Atom optimizations in order to make binaries executable in buildroot
export RPM_OPT_FLAGS=`echo $RPM_OPT_FLAGS | sed s'/atom/i686/g'`
export CFLAGS=`echo $CFLAGS | sed s'/atom/i686/g'`
export CXXFLAGS=`echo $CXXFLAGS | sed s'/atom/i686/g'`

export CFLAGS+=" -ffat-lto-objects"
export CXXFLAGS+=" -ffat-lto-objects"

%ifarch aarch64
export CFLAGS="$CFLAGS -fno-lto"
export CXXFLAGS="$CXXFLAGS -fno-lto"
%endif

# Needed by patch0
NOCONFIGURE=1 ./autogen.sh
%configure \
    --with-pic \
    --enable-fc \
    --enable-ft \
%if %{with cairo_gl_backend}
    --enable-egl=yes \
    --enable-glesv2=yes \
    --enable-evasgl=yes \
%endif
    --enable-ps \
    --enable-pdf \
    --enable-script \
    --enable-svg \
    --enable-tee \
%if %{with wayland} && !%{with x}
   --disable-xlib \
   --disable-xcb  \
%else
    --enable-xlib \
%if %{with cairo_xcb_backend}
    --enable-xcb \
%endif
%endif
    --disable-gtk-doc \
    --disable-static
make %{?_smp_mflags} V=1

%install
%make_install

%post -n libcairo -p /sbin/ldconfig

%postun -n libcairo -p /sbin/ldconfig

%post -n libcairo-gobject -p /sbin/ldconfig

%postun -n libcairo-gobject -p /sbin/ldconfig

%post -n libcairo-script-interpreter -p /sbin/ldconfig

%postun -n libcairo-script-interpreter -p /sbin/ldconfig

%files -n libcairo
%manifest %{name}.manifest
%defattr(-, root, root)
%license COPYING COPYING-LGPL-2.1 COPYING-MPL-1.1
%{_libdir}/libcairo.so.*

%files -n libcairo-gobject
%manifest %{name}.manifest
%defattr(-, root, root)
%{_libdir}/libcairo-gobject.so.2*

%files -n libcairo-script-interpreter
%manifest %{name}.manifest
%defattr(-, root, root)
%license util/cairo-script/COPYING
%{_libdir}/libcairo-script-interpreter.so.*

%files -n tools
%manifest %{name}.manifest
%defattr(-, root, root)
%license util/cairo-trace/COPYING util/cairo-trace/COPYING-GPL-3
%{_bindir}/cairo-sphinx
%{_bindir}/cairo-trace
%dir %{_libdir}/cairo
%{_libdir}/cairo/cairo-fdr.so*
%{_libdir}/cairo/cairo-sphinx.so*
%{_libdir}/cairo/libcairo-trace.so*

%files devel
%manifest %{name}.manifest
%defattr(-, root, root)
#%doc PORTING_GUIDE
%{_includedir}/cairo/
#%doc %{_datadir}/gtk-doc/html/cairo
%{_libdir}/*.so
%{_libdir}/pkgconfig/*.pc

%changelog
