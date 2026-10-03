# GitHub research and reuse

## Decisions

The CAD kernel is OCCT. Its algorithms and native AIS selection are reused
directly rather than replaced with mesh intersection, handwritten raycasts,
bespoke arc-length solvers or approximated triangle normals. Qt supplies docking,
tree widgets, futures, undo commands, file dialogs and deployment tools.

The project is independent from ABB and RoboDK. The existing local PoC contributes
only an already built OCCT SDK and matching official OCCT source tree.
No PoC plugin, robot API or proprietary host resource is linked.

## Primary source references

- [Official OCCT repository, V7_6_0](https://github.com/Open-Cascade-SAS/OCCT/tree/V7_6_0).
- [OCCT native Qt sample](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/samples/qt/Common/src/View.cxx): QWidget/native window integration.
- [gkv311/occt-samples-qt](https://github.com/gkv311/occt-samples-qt): established Qt5/Qt6 OCCT integration choices, MIT.
- [Mayo](https://github.com/fougue/mayo): mature Qt/OCCT CAD viewer, XCAF assembly and STEP integration, BSD-2-Clause.
- [Mayo STEP importer](https://github.com/fougue/mayo/blob/develop/src/io_occ/io_occ_step.cpp).
- [STEPCAFControl_Reader implementation](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/STEPCAFControl/STEPCAFControl_Reader.cxx): document target units must be set before Transfer.
- [BRepAlgoAPI_Section](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/BRepAlgoAPI/BRepAlgoAPI_Section.hxx): exact selected trimmed Face/plane intersection and ancestry.
- [BRep_Tool](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/BRep/BRep_Tool.hxx): surface locations, pcurves and periodic seams.
- [GCPnts_UniformAbscissa](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/GCPnts/GCPnts_UniformAbscissa.hxx): real 3D curve distance sampling.
- [GCPnts_TangentialDeflection](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/GCPnts/GCPnts_TangentialDeflection.hxx): curvature and deflection discretization.
- [BRepLProp_SLProps](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/BRepLProp/BRepLProp_SLProps.hxx): CAD surface derivative normals.
- [BRepClass_FaceClassifier](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/BRepClass/BRepClass_FaceClassifier.hxx): trim-aware UV validation.
- [BRepBuilderAPI_MakeFace](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/BRepBuilderAPI/BRepBuilderAPI_MakeFace.hxx) and [GeomAPI_PointsToBSplineSurface](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_6_0/src/GeomAPI/GeomAPI_PointsToBSplineSurface.hxx): analytic/freeform test data.

All implementation calls were checked against the actual installed 7.6 headers;
the online current API documentation can describe a newer, incompatible version.
The Qt examples and Mayo guided integration architecture; their application source
was not copied into this project. The TKOpenGl fallback compiles official OCCT
source unchanged and preserves the OCCT license.

## Important implementation findings

XCAF transfer derives its target length unit from the document, so calling only
STEPControl_Reader::SetSystemLengthUnit is insufficient. The document target is
millimetres before transfer; input unit metadata is still retained.

Section ancestry is absent for some original/common boundary edges. The fallback
checks projected curve points against actual trimmed Faces. AIS Face owners can
carry locations: identity uses the located TopoDS_Face and indexed topology map,
not raw pointers or tessellation indices.

Region margins exclude common selected boundaries and periodic seams. Shared
topology and endpoint smoothness are required before merging different Face
curves. Separate islands and holes preserve segment boundaries in display/export.

Native OCCT QWidget windows are absent from QWidget::grab(), so automated visual
verification renders the viewport through OCCT and composes it into the actual
Qt window layout.

With automatic selection disabled and Display(selectionMode=-1), OCCT 7.6 must
Load the object into the selection manager before Activate. Otherwise modes and
sensitive primitives exist but the selection state remains Unknown and no mouse
position can pick a Face. Desktop mouse-event acceptance exposed this issue.

Selected-face overlays reuse the root model's display tessellation. Remeshing a
single Face independently can change deflection and cause depth artifacts against
the root's cached mesh. Native render capture reads Image_PixMap RGBA rows directly
into QImage, avoiding the no-FreeImage PPM writer's post-maxval comment header.
