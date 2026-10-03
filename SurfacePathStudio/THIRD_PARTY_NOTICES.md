# Third-party notices

## Open CASCADE Technology

The application links the local OCCT 7.6.0 static SDK. The optional TKOpenGl bridge
compiles the matching official module. OCCT is distributed under LGPL-2.1 with the
Open CASCADE exception. Preserve both texts included under `licenses/`.

Source: https://github.com/Open-Cascade-SAS/OCCT/tree/V7_6_0

The reused SDK was prepared by the existing local OCCT build. It includes a
Font_FTFont backport from official OCCT 7.6.3 to support a no-FreeType build.
It is a CAD dependency, not a RoboDK plugin dependency.

## Qt

The application uses the user's installed Qt SDK. Its applicable license texts
are copied from that installation for local deployment. Source and license:
https://www.qt.io/licensing/ and https://code.qt.io/

## Microsoft Visual C++ runtime

The local deployment includes the x64 Release CRT DLLs from the installed
Visual Studio 2019 toolchain's redistributable directory. They remain subject
to Microsoft's Visual Studio license and redistributable terms.

## Research references

gkv311/occt-samples-qt (MIT) and fougue/mayo (BSD-2-Clause) were researched for
native Viewer/XCAF integration patterns. No application source from these
repositories is redistributed here. Their licenses remain at their repositories:

- https://github.com/gkv311/occt-samples-qt/blob/master/LICENSE.txt
- https://github.com/fougue/mayo/blob/develop/LICENSE.txt

No ABB code, icons or UI resources, RoboDK SDK, Python, Open3D or VTK are part of
this application's dependencies.
