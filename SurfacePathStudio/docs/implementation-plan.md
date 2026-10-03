# SurfacePathStudio implementation plan — 2026-10-03

The user's AGENT.md is the accepted functional specification. The application is
standalone C++17, Qt Widgets (Qt 5 and 6), and OCCT. No ABB or RoboDK dependency.

## Architecture and reuse

Use OCCT for CAD topology, STEP/XCAF, native AIS selection, plane sections,
arc-length sampling, surface projection and derivatives. Reference official OCCT
samples and Mayo for established integration practices. Do not duplicate a CAD
kernel, tessellation intersection algorithm, or UI framework.

CAD/Viewer agent owns `src/cad` and `src/viewer`. Geometry agent owns
`src/geometry` and the path implementations. Verification agent owns `tests` and
`examples` generation. The coordinator owns shared PathTypes, CMake, native build
scripts, app, UI, exports, documentation and integration. File ownership prevents
parallel edits from overwriting each other.

## Ordered validation gates

1. Build and launch the Qt/OCCT viewer with STEP/XCAF import.
2. Verify native Face/Edge selection, runtime IDs, area, and highlighting.
3. Verify a single B-Rep section of a selected trimmed Face.
4. Verify parallel plane intersections with physical plane spacing.
5. Verify arc-length/adaptive samples, UV classification, oriented CAD normals.
6. Verify zigzag, disconnected segments, holes and shared-topology multi-Face joins.
7. Verify right-handed TCP frames, quaternion continuity, lead/tilt and contact/TCP offset.
8. Integrate parameter panel, worker cancellation, preview, project paths and visibility.
9. Verify CSV, six analytic/freeform fixtures, documentation and runnable deployment.

Geometry regression tests compare against analytic surfaces and OCCT Face
classification, not implementation internals. Full spheres and cylinders require
an explicit reference normal when their normal average is not unique.

## Coordinate and process semantics

All geometry and distances are millimetres. Stepover is Euclidean distance
between section planes. Boundary margin is shortest 3D distance to region boundary
edges, not a geodesic offset. A hole, island or rejected boundary sample starts a
new process segment. The 0.1.1 follow-up adds straight Transfer links between
successive TCP endpoints, including Depart-to-Approach when extensions are used.
These links are explicit non-process motion, without clearance or collision planning.
Tangent extensions are Approach/Depart, never on-surface Process moves.

TCP Z follows or opposes the oriented CAD normal. X follows the path tangent
projected into the tangent plane, Y = Z cross X. Lead rotates about local Y,
then tilt about the resulting local X. Contact points remain on the Face; TCP
points apply offset along the surface normal. Exported quaternion order is XYZW.

## Deferred features

True geodesic stepover, surface-constrained extension, robot-specific code,
collision/IK and full undo persistence are not first-version implementations.
Their interfaces or explicit disabled states must not imply functionality.
