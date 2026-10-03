



请帮我创建一个完整、可编译运行的桌面 CAD/机器人曲面路径生成软件。

这个软件的交互方式和功能目标可以参考 ABB RobotStudio Machining PowerPac 的“选择 CAD 曲面 → 设置路径参数 → 预览 → 生成曲面加工路径”的工作流，但不要复制 ABB 的 UI、资源、代码或假设其内部算法。

我要实现的是一个独立软件。

# 1. 软件目标

软件需要支持以下完整流程：

```text_6fS8_0
导入 STEP / STP
        ↓
显示完整 CAD B-Rep 模型
        ↓
鼠标点击选择一个或多个真正的 CAD Face
        ↓
高亮选中的曲面
        ↓
设置路径参数
        ↓
直接基于选中的 B-Rep 曲面生成路径
        ↓
自动计算曲面法向
        ↓
自动生成 TCP 姿态
        ↓
3D 预览路径和姿态
        ↓
导出机器人路径
```

典型用途：

- 机器人打磨
- 抛光
- 喷涂
- 扫描
- 超声检测
- 去毛刺
- 曲面加工
- 机器人表面处理

最重要的要求：

**路径计算必须基于 STEP 的 B-Rep 曲面，而不是先把 STEP 转成 STL 再规划。**

允许 OpenCASCADE 为显示生成 triangulation，但 triangulation 只能用于显示/拾取加速，不能作为主要曲面路径计算数据。

------

# 2. 技术栈

使用：

```text_6fS8_0
C++17 或 C++20
Qt 5
Open CASCADE Technology / OCCT
CMake
```

同时尽量兼容更新版本。

不要使用：

```text_6fS8_0
Python
Open3D
VTK
RoboDK SDK
ABB SDK
```

核心 CAD 与几何算法全部使用 OpenCASCADE。

如果 OCCT 不同版本 API 有变化，请根据实际安装版本调整，不能为了完全照提示词而写无法编译的 API。

------

# 3. 软件架构

按照可维护的工程结构创建：

```text_6fS8_0
SurfacePathStudio/
│
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
│
├── src/
│   ├── main.cpp
│   │
│   ├── app/
│   │   ├── MainWindow.h
│   │   └── MainWindow.cpp
│   │
│   ├── viewer/
│   │   ├── OcctViewWidget.h
│   │   ├── OcctViewWidget.cpp
│   │   ├── SelectionManager.h
│   │   ├── SelectionManager.cpp
│   │   ├── PathPresentation.h
│   │   └── PathPresentation.cpp
│   │
│   ├── cad/
│   │   ├── CadDocument.h
│   │   ├── CadDocument.cpp
│   │   ├── StepImporter.h
│   │   ├── StepImporter.cpp
│   │   ├── FaceRepository.h
│   │   └── FaceRepository.cpp
│   │
│   ├── geometry/
│   │   ├── SurfaceUtils.h
│   │   ├── SurfaceUtils.cpp
│   │   ├── SurfaceNormal.h
│   │   ├── SurfaceNormal.cpp
│   │   ├── CurveSampler.h
│   │   └── CurveSampler.cpp
│   │
│   ├── path/
│   │   ├── PathTypes.h
│   │   ├── PathPlanner.h
│   │   ├── SectionPathPlanner.h
│   │   ├── SectionPathPlanner.cpp
│   │   ├── PathOrdering.h
│   │   ├── PathOrdering.cpp
│   │   ├── PoseGenerator.h
│   │   └── PoseGenerator.cpp
│   │
│   ├── ui/
│   │   ├── PathParameterPanel.h
│   │   ├── PathParameterPanel.cpp
│   │   ├── ModelTreeWidget.h
│   │   └── ModelTreeWidget.cpp
│   │
│   └── export/
│       ├── CsvExporter.h
│       ├── CsvExporter.cpp
│       ├── RobotPathExporter.h
│       └── RobotPathExporter.cpp
│
└── tests/
    ├── test_plane.cpp
    ├── test_cylinder.cpp
    ├── test_bspline.cpp
    └── test_multiface.cpp
```

类名允许根据实际代码调整，但不要把所有功能塞进 `MainWindow.cpp`。

CAD、UI、显示、路径算法、姿态计算、导出必须解耦。

------

# 4. STEP/STP 导入

支持：

```text_6fS8_0
.step
.stp
```

优先使用：

```cpp_ZBwg_1
STEPCAFControl_Reader
```

配合 XCAF。

如果完整 XCAF 集成导致第一版本明显复杂，也可以先使用：

```cpp_ZBwg_1
STEPControl_Reader
```

但整体架构必须允许以后切换为 XCAF。

希望最终能够保留：

- Assembly hierarchy
- Shape
- Name
- Color
- Face topology

导入完成后保存真正的：

```cpp_ZBwg_1
TopoDS_Shape
```

不能只保存 triangulation。

内部统一使用 mm。

如果 STEP 文件存在单位信息，应正确进行单位处理。

不要简单假定所有 STEP 的原始单位一定是 mm。

------

# 5. 3D CAD Viewer

使用 OCCT Viewer：

```text_6fS8_0
V3d_Viewer
V3d_View
AIS_InteractiveContext
AIS_Shape
```

与 Qt QWidget 集成。

至少实现：

- Fit All
- Rotate
- Pan
- Zoom
- Isometric View
- Front
- Back
- Top
- Bottom
- Left
- Right
- Shaded
- Wireframe
- Show/Hide Model
- Reset View

背景使用正常 CAD 软件风格即可。

------

# 6. 非常重要：Face Selection

用户必须可以直接用鼠标点击 STEP 模型中的曲面。

选择级别设置为：

```text_6fS8_0
FACE
```

得到：

```cpp_ZBwg_1
TopoDS_Face
```

而不是整个 Solid。

支持：

```text_6fS8_0
左键：选择 Face
Ctrl + 左键：增加/取消 Face
Esc：清空选择
```

选中的 Face 必须高亮。

例如：

```text_6fS8_0
原模型：灰色
Selected Face：明显高亮
Generated Path：另一明显颜色
```

UI 显示：

```text_6fS8_0
Selected faces: 5
Total selected area: 3521.72 mm²
```

点击曲面后能够识别：

```text_6fS8_0
Plane
Cylinder
Cone
Sphere
Torus
Bezier Surface
BSpline Surface
Surface of Revolution
Surface of Extrusion
Other
```

可以使用：

```cpp_ZBwg_1
BRepAdaptor_Surface
```

识别基本曲面类型。

------

# 7. Face ID 管理

导入模型后：

```cpp_ZBwg_1
TopTools_IndexedMapOfShape faceMap;
TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
```

为每个 Face 建立稳定的运行时 Face ID。

例如：

```text_6fS8_0
Face 1
Face 2
Face 3
...
```

SelectionManager 中保存：

```cpp_ZBwg_1
std::vector<int> selectedFaceIds;
```

并能够取得对应：

```cpp_ZBwg_1
TopoDS_Face
```

不要依赖裸指针判断 Face 身份。

------

# 8. 多曲面选择

不仅支持单 Face。

用户应该可以选择：

```text_6fS8_0
Face A
Face B
Face C
```

然后把它们作为一个加工区域：

```text_6fS8_0
SelectedSurfaceRegion
```

数据结构建议：

```cpp_ZBwg_1
struct SelectedSurfaceRegion
{
    std::vector<TopoDS_Face> faces;
};
```

如果这些面相邻，生成路径时尽可能在它们之间连续。

暂时不要求做到完美跨 Patch geodesic，但不能因为存在多个 Face 就直接失败。

------

# 9. 可选：选择相邻曲面

增加功能：

```text_6fS8_0
Select Tangent Faces
```

用户先选择一个 Face。

软件寻找共享 Edge 的邻接 Face。

如果公共边附近两面的法向夹角小于：

```text_6fS8_0
tangentAngleTolerance
```

例如：

```text_6fS8_0
5°
```

自动把邻接 Face 加入 Selection。

这样可以快速选取一个由多个 NURBS patch 组成的连续自由曲面。

------

# 10. 路径生成参数面板

右侧创建：

```text_6fS8_0
Surface Path
```

参数面板。

包含：

```text_6fS8_0
Strategy
Path Direction
Machining Angle
Step Over
Point Distance
Boundary Margin
Path Direction Mode
Tool Normal Direction
Lead Angle
Tilt Angle
Z Offset
Start Side
Snake / One Way
Extension Distance
Chord Tolerance
Normal Angle Tolerance
```

例如默认：

```text_6fS8_0
Strategy            Parallel Sections
Machining Angle     0 deg
Step Over           10 mm
Point Distance      2 mm
Boundary Margin     0 mm
Pattern             Zig-Zag
Tool Z              Surface Normal
Lead Angle          0 deg
Tilt Angle          0 deg
Z Offset            0 mm
```

------

# 11. 路径方向定义

用户必须能够控制路径方向。

至少实现以下模式：

```text_6fS8_0
Global X
Global Y
Global Z
Selected Edge
Two Picked Points
Custom Vector
```

最重要的是：

```text_6fS8_0
Selected Edge
```

模式。

用户先选择一条 CAD Edge。

用该 Edge 的切线或者首尾方向作为主要路径方向。

也允许通过两个 3D 点：

```text_6fS8_0
P1
P2
```

定义：

```cpp_ZBwg_1
T = normalize(P2 - P1);
```

------

# 12. Machining Angle

类似 CAM/RobotStudio 使用方式。

在基准路径方向基础上支持：

```text_6fS8_0
Machining Angle
```

例如：

```text_6fS8_0
0°
30°
45°
90°
```

用于旋转路径方向。

------

# 13. 第一版核心算法

第一版不要再采用：

```text_6fS8_0
在 XY 平面生成 raster
然后射线投影到 STL
```

核心策略实现为：

```text_6fS8_0
Parallel Section Surface Path
```

也就是：

**平行截面法。**

------

# 14. Parallel Section 算法思想

假设：

```text_6fS8_0
C = selected surface region center
N = selected region reference normal
T = desired path direction
```

建立：

```text_6fS8_0
B = normalize(N × T)
```

然后重新正交：

```text_6fS8_0
T = normalize(B × N)
```

得到局部坐标：

```text_6fS8_0
T = 路径大致前进方向
B = 路径横向/Stepover方向
N = 曲面参考法向
```

创建一系列互相平行的切割平面：

```text_6fS8_0
Plane_i
```

平面法向：

```text_6fS8_0
B
```

所以：

```text_6fS8_0
Plane_i:
B · (X - C) = d_i
```

其中：

```text_6fS8_0
d_i = d_min + i * stepOver
```

即每个切片平面的空间距离为：

```text_6fS8_0
stepOver
```

------

# 15. 获取 Selected Region 范围

不要固定 width 和 height。

根据用户选择的实际 Faces 自动计算。

计算 selected faces 的 Bounding Box：

```cpp_ZBwg_1
Bnd_Box
```

把 bbox corners 或曲面采样点转换到：

```text_6fS8_0
T/B/N
```

局部坐标系。

找到：

```text_6fS8_0
B_min
B_max
```

然后自动生成：

```text_6fS8_0
B_min
B_min + stepOver
B_min + 2*stepOver
...
B_max
```

对应的一系列 slicing planes。

------

# 16. B-Rep 与切割平面求交

把所有 selected faces 组成：

```cpp_ZBwg_1
TopoDS_Compound
```

然后每一个 slicing plane 与 selected region 做精确 B-Rep intersection。

优先使用 OCCT：

```text_6fS8_0
BRepAlgoAPI_Section
```

或者当前 OCCT 版本最合适的 Surface/Plane intersection API。

不要自己写三角形求交。

求交结果应得到：

```text_6fS8_0
TopoDS_Edge
TopoDS_Wire
```

这些交线天然位于选中的 CAD 曲面上。

所以：

```text_6fS8_0
selected B-Rep surface
        ∩
section plane
        ↓
3D machining curve
```

------

# 17. Trimmed Surface

非常重要。

STEP 中很多曲面都是：

```text_6fS8_0
Underlying NURBS surface
+
trim boundary
```

路径只能存在于：

```text_6fS8_0
TopoDS_Face
```

有效修剪区域内部。

不能只取得：

```cpp_ZBwg_1
Geom_Surface
```

然后在整个无限/未修剪曲面上生成路径。

必须尊重：

- Outer wire
- Inner wire
- Holes
- Trim boundaries

使用 B-Rep section 的目的之一就是自动尊重这些边界。

------

# 18. 带孔区域

例如选择曲面：

```text_6fS8_0
+-------------------+
|                   |
|       +----+      |
|       |孔洞|      |
|       +----+      |
|                   |
+-------------------+
```

某一个 section plane 可能得到：

```text_6fS8_0
Segment A
Segment B
```

而不是一条完整路径。

必须允许：

```cpp_ZBwg_1
std::vector<PathSegment>
```

一层中存在多个 segment。

不要错误连接穿过孔洞。

------

# 19. 路径排序

对每个 slicing plane 得到的所有 curves：

按照 T 方向排序。

例如奇数行：

```text_6fS8_0
→ → → → →
```

偶数行：

```text_6fS8_0
← ← ← ← ←
```

实现：

```text_6fS8_0
Zig-Zag
```

模式。

数据结构：

```cpp_ZBwg_1
struct PathLayer
{
    int layerIndex;
    std::vector<PathSegment> segments;
};
```

和：

```cpp_ZBwg_1
struct PathSegment
{
    int layerIndex;
    int segmentIndex;

    std::vector<PathPoint> points;
};
```

------

# 20. 不允许错误连接

下面两个曲面区域如果不连续：

```text_6fS8_0
──────        ──────
```

不要生成：

```text_6fS8_0
──────XXXXXXXX──────
```

这种穿过空气的加工路径。

应保存为两个：

```text_6fS8_0
Process Segment
```

如果未来需要连接，可以创建：

```text_6fS8_0
Transfer Move
```

而不是 Process Move。

定义：

```cpp_ZBwg_1
enum class MotionType
{
    Process,
    Transfer,
    Approach,
    Depart
};
```

------

# 21. Curve Sampling

从 BRep section 得到 Edge 后，对曲线采样。

不能简单按曲线参数：

```text_6fS8_0
u += constant
```

因为 NURBS 参数与实际空间距离不成正比。

目标是：

```text_6fS8_0
Point Distance ≈ 2 mm
```

使用 OCCT 弧长工具。

可以使用：

```text_6fS8_0
GCPnts_UniformAbscissa
GCPnts_QuasiUniformAbscissa
```

配合：

```text_6fS8_0
BRepAdaptor_Curve
```

按真实 3D curve arc length 采样。

要求：

```text_6fS8_0
相邻采样点距离尽量接近 pointDistance
```

------

# 22. 自适应采样

除了：

```text_6fS8_0
pointDistance
```

还要考虑曲率和法向变化。

如果：

```text_6fS8_0
相邻 normal angle > normalAngleTolerance
```

例如：

```text_6fS8_0
3°
```

则在中间插点。

如果：

```text_6fS8_0
chord error > chordTolerance
```

例如：

```text_6fS8_0
0.1 mm
```

也进行细分。

因此最终采样条件是：

```text_6fS8_0
距离限制
+
弦误差限制
+
法向变化限制
```

这样自由曲面上的机器人姿态更加平滑。

------

# 23. 获取 PathPoint 对应的 Face

Section 可能跨越多个 selected faces。

每个 PathPoint 必须知道自己属于哪个 Face。

建议保存：

```cpp_ZBwg_1
struct SurfaceLocation
{
    int faceId;
    double u;
    double v;
};
```

最终：

```cpp_ZBwg_1
struct PathPoint
{
    gp_Pnt position;

    gp_Dir normal;
    gp_Dir tangent;

    double u;
    double v;

    int faceId;
    int layerIndex;
    int segmentIndex;
};
```

------

# 24. 求 UV

对于 3D PathPoint：

```text_6fS8_0
P
```

求它在对应 Face underlying surface 上的：

```text_6fS8_0
u,v
```

可以使用适合当前 OCCT 版本的：

```text_6fS8_0
GeomAPI_ProjectPointOnSurf
ShapeAnalysis_Surface
```

或者从 section 算法直接保留 face/UV correspondence。

优先选择数值稳定的方法。

必须验证最终：

```text_6fS8_0
P
```

确实处于：

```text_6fS8_0
Face interior 或 Face boundary
```

附近。

------

# 25. 精确计算曲面法向

不要用 STL 三角面 normal。

使用真正 CAD surface derivative。

对于：

```text_6fS8_0
(u,v)
```

计算：

```text_6fS8_0
dP/du
dP/dv
```

然后：

```text_6fS8_0
N = normalize(dP/du × dP/dv)
```

建议使用：

```text_6fS8_0
BRepLProp_SLProps
```

或者：

```text_6fS8_0
GeomLProp_SLProps
```

获取：

```text_6fS8_0
Normal
D1
Curvature
```

------

# 26. Face Orientation

OpenCASCADE 中：

```cpp_ZBwg_1
TopoDS_Face::Orientation()
```

可能是：

```text_6fS8_0
TopAbs_FORWARD
TopAbs_REVERSED
```

所以 underlying surface normal 并不一定就是拓扑 Face 的外向 normal。

如果：

```cpp_ZBwg_1
face.Orientation() == TopAbs_REVERSED
```

必须正确翻转 normal。

这一点必须写单元测试。

------

# 27. Normal Continuity

路径 normal 不允许：

```text_6fS8_0
↑ ↑ ↑ ↓ ↑ ↑
```

这种随机翻转。

除了 Face orientation 外，还需要 sequential continuity。

如果：

```cpp_ZBwg_1
normal_i.Dot(normal_previous) < 0
```

则：

```text_6fS8_0
normal_i = -normal_i
```

但是跨多个 Face 时，首先应依据 B-Rep orientation 保持正确的拓扑方向，再使用连续性策略处理可能的模型问题。

------

# 28. Tool Z

参数：

```text_6fS8_0
Tool Z Direction
```

支持：

```text_6fS8_0
Along Surface Normal
Against Surface Normal
```

定义：

```cpp_ZBwg_1
toolZ = sign * surfaceNormal;
```

其中：

```text_6fS8_0
sign = +1
或
sign = -1
```

------

# 29. Path Tangent

从 section curve 本身计算路径 tangent。

优先使用真正曲线导数。

如果某些位置导数不可用，可以通过相邻点：

```cpp_ZBwg_1
T = normalize(P[i+1] - P[i-1]);
```

首点：

```cpp_ZBwg_1
T = normalize(P[1] - P[0]);
```

末点：

```cpp_ZBwg_1
T = normalize(P[n-1] - P[n-2]);
```

------

# 30. TCP Orientation

构造机器人 TCP coordinate frame。

默认：

```text_6fS8_0
Z_tool = surface normal
X_tool = path tangent projected onto tangent plane
Y_tool = Z_tool × X_tool
X_tool = Y_tool × Z_tool
```

具体：

```cpp_ZBwg_1
Z = normalize(toolZ);

X0 = pathTangent;

X = normalize(
    X0 - dot(X0, Z) * Z
);

Y = normalize(Z.Crossed(X));

X = normalize(Y.Crossed(Z));
```

必须保证：

```text_6fS8_0
X ⟂ Y
Y ⟂ Z
X ⟂ Z
```

并且是右手系。

------

# 31. Lead Angle / Tilt Angle

支持机器人加工常用姿态偏置：

```text_6fS8_0
Lead Angle
Tilt Angle
```

例如：

```text_6fS8_0
Lead = 10°
Tilt = 5°
```

在基础 surface frame 上施加旋转。

定义要在代码和 UI 中明确：

```text_6fS8_0
Lead：绕局部 Y 轴旋转
Tilt：绕局部 X 轴旋转
```

或者反过来，但必须保持定义一致并写进 README。

最后重新正交化。

------

# 32. Tool Offset

支持：

```text_6fS8_0
Z Offset
```

例如：

```text_6fS8_0
0 mm
2 mm
-1 mm
```

路径位置：

```cpp_ZBwg_1
toolPoint =
    surfacePoint +
    zOffset * surfaceNormal;
```

注意同时保留：

```text_6fS8_0
Contact Point
```

和：

```text_6fS8_0
TCP Point
```

不要覆盖原始 surface contact position。

数据结构建议：

```cpp_ZBwg_1
struct RobotTarget
{
    gp_Pnt contactPoint;
    gp_Pnt tcpPosition;

    gp_Dir surfaceNormal;

    gp_Dir toolX;
    gp_Dir toolY;
    gp_Dir toolZ;

    gp_Quaternion orientation;
};
```

------

# 33. Boundary Margin

增加：

```text_6fS8_0
Boundary Margin
```

第一版可以采用：

```text_6fS8_0
3D shortest distance to selected Face boundary edges
```

作为近似。

如果 PathPoint 到 Face 外边界或孔边界距离：

```text_6fS8_0
< boundaryMargin
```

则移除。

例如：

```text_6fS8_0
Boundary Margin = 5 mm
```

路径至少尽量离边界 5 mm。

README 必须注明：

第一版这是 3D Euclidean boundary distance，不是 surface geodesic boundary distance。

------

# 34. Extension

支持：

```text_6fS8_0
Extension Distance
```

例如：

```text_6fS8_0
5 mm
```

用于把每条路径的首尾沿 tangent 延长。

但 extension 后的点如果离开曲面：

提供两种模式：

```text_6fS8_0
None
Tangent Extension
Surface Constrained
```

第一版允许先实现：

```text_6fS8_0
None
```

和：

```text_6fS8_0
Tangent Extension
```

Surface Constrained 可以预留接口。

------

# 35. Preview / Generate 工作流

UI 不要修改参数就立即执行大量计算。

使用两个按钮：

```text_6fS8_0
Preview
Generate
```

点击：

```text_6fS8_0
Preview
```

生成临时路径并显示。

点击：

```text_6fS8_0
Generate
```

确认保存进项目路径树。

------

# 36. 路径 3D 显示

生成后显示：

```text_6fS8_0
CAD Surface
+ Path Curve
+ Direction Arrows
+ Tool Orientation
```

路径可以用高亮 polyline。

每隔：

```text_6fS8_0
10 或 20 个点
```

显示一个 tool frame。

例如：

```text_6fS8_0
       Z
       ↑
       |
       ●----→ X
      /
     Y
```

支持开关：

```text_6fS8_0
Show Path
Show Contact Points
Show Surface Normals
Show TCP Frames
Show Layer Numbers
```

------

# 37. 起点方向

允许：

```text_6fS8_0
Start From
```

设置：

```text_6fS8_0
Min B
Max B
```

以及：

```text_6fS8_0
First Row Direction:
Positive T
Negative T
```

用户可以决定机器人从哪侧开始。

------

# 38. Zig-Zag 与 One-Way

支持：

```text_6fS8_0
Zig-Zag
```

例如：

```text_6fS8_0
→→→→→
←←←←←
→→→→→
←←←←←
```

和：

```text_6fS8_0
One-Way
```

例如：

```text_6fS8_0
→→→→→
→→→→→
→→→→→
```

One-Way 模式下相邻 Process Segment 之间必须被视为：

```text_6fS8_0
Transfer Move
```

而不是直接加工。

------

# 39. Selected Surface Reference Normal

自由曲面不存在一个全局唯一 normal。

所以 `N` 只能用于构造 section frame。

实现方法：

在 selected region 内采样多个点。

计算面积加权平均 normal：

```text_6fS8_0
N_reference
```

如果平均值接近零，例如选择了一个近乎封闭的区域，则不能继续自动推断。

UI 提示：

```text_6fS8_0
Unable to determine a unique reference normal.
Please specify a reference direction.
```

允许用户手工提供：

```text_6fS8_0
Reference Normal X/Y/Z
```

------

# 40. Path Direction 投影

用户输入 global/path direction：

```text_6fS8_0
D
```

不能直接把 D 当作 tangent。

应该：

```cpp_ZBwg_1
T =
D - dot(D, N_reference) * N_reference;
```

如果：

```text_6fS8_0
|T| < epsilon
```

说明路径方向几乎平行于 normal。

UI 报错：

```text_6fS8_0
Path direction is parallel to the reference normal.
Choose another direction.
```

------

# 41. 多 Face 跨界连续性

如果一个 section curve 从：

```text_6fS8_0
Face A
```

进入：

```text_6fS8_0
Face B
```

并且两个 Face 共边且几何连续，则尽量合并成一条：

```text_6fS8_0
PathSegment
```

允许位置上的小 tolerance：

```text_6fS8_0
joinTolerance = 0.01 ~ 0.1 mm
```

但不能只因为两个 curve endpoint 靠近就强行连接。

还要验证：

```text_6fS8_0
shared topology
或
几何连续性
```

------

# 42. 孔洞和岛

程序必须正确处理：

```text_6fS8_0
Outer boundary
Inner holes
Multiple islands
```

一个 section layer 中可能出现：

```text_6fS8_0
1
2
3
...
```

多个 curve segment。

这些必须保留。

------

# 43. Surface Path 数据模型

建议：

```cpp_ZBwg_1
struct PathPoint
{
    gp_Pnt surfacePoint;
    gp_Pnt tcpPoint;

    gp_Dir tangent;
    gp_Dir surfaceNormal;

    gp_Dir toolX;
    gp_Dir toolY;
    gp_Dir toolZ;

    gp_Quaternion orientation;

    double u = 0.0;
    double v = 0.0;

    int faceId = -1;
    int layerId = -1;
    int segmentId = -1;
    int pointId = -1;
};
```

以及：

```cpp_ZBwg_1
struct PathSegment
{
    int layerId;
    int segmentId;

    MotionType type;

    std::vector<PathPoint> points;
};
```

以及：

```cpp_ZBwg_1
struct SurfacePath
{
    std::vector<PathSegment> segments;

    double totalProcessLength;
    double totalTransferLength;
};
```

------

# 44. PathPlanner 抽象接口

定义：

```cpp_ZBwg_1
class PathPlanner
{
public:
    virtual ~PathPlanner() = default;

    virtual SurfacePath generate(
        const SelectedSurfaceRegion& region,
        const PathParameters& params) = 0;
};
```

当前实现：

```cpp_ZBwg_1
class SectionPathPlanner : public PathPlanner
```

以后预留：

```cpp_ZBwg_1
class ProjectionPathPlanner;
class IsoParametricPathPlanner;
class GeodesicOffsetPathPlanner;
class ContourParallelPathPlanner;
```

------

# 45. 非常重要：Step Over 的定义

第一版：

```text_6fS8_0
Step Over = 相邻 slicing planes 的 Euclidean distance
```

例如：

```text_6fS8_0
10 mm
```

这比原来的平面射线投影方法更适合自由曲面。

但是必须在 README 中明确：

这仍然不等于严格的：

```text_6fS8_0
Surface geodesic distance = 10 mm
```

对于高度倾斜或高曲率区域，实际沿曲面测量的距离会大于平面间距。

禁止声称第一版已经实现“严格曲面等距”。

------

# 46. 第二阶段预留：True Surface Offset

架构中预留：

```cpp_ZBwg_1
class GeodesicOffsetPathPlanner
```

未来用于：

```text_6fS8_0
Seed curve
    ↓
Surface geodesic offset
    ↓
Offset curve 1
    ↓
Offset curve 2
    ↓
...
```

最终目标：

```text_6fS8_0
沿曲面测量的 Stepover ≈ constant
```

但第一版先不要为了实现 geodesic offset 而破坏项目稳定性。

------

# 47. 可选第二策略：UV Iso Path

可以在 Parallel Section 完成后增加：

```text_6fS8_0
Iso U
Iso V
```

策略。

但是必须正确处理：

```text_6fS8_0
Trimmed Face
```

和：

```text_6fS8_0
UV distortion
```

UI 中明确说明：

```text_6fS8_0
UV parameter spacing does not imply physical equal spacing.
```

因此不要把 UV 参数差：

```text_6fS8_0
Δu = constant
```

错误地叫作：

```text_6fS8_0
10 mm stepover
```

------

# 48. Path Project Tree

左侧项目树类似：

```text_6fS8_0
Project
├── CAD
│   └── Part.step
│
├── Surface Selections
│   └── SurfaceRegion_1
│       ├── Face 12
│       ├── Face 13
│       └── Face 18
│
└── Paths
    ├── SurfacePath_1
    └── SurfacePath_2
```

点击：

```text_6fS8_0
SurfaceRegion_1
```

高亮对应 Face。

点击：

```text_6fS8_0
SurfacePath_1
```

高亮路径。

支持：

```text_6fS8_0
Rename
Delete
Hide
Show
Regenerate
```

------

# 49. Surface Path Wizard

可以参考工业软件工作流做一个简单 Wizard：

```text_6fS8_0
1. Select Surface
2. Define Direction
3. Set Process Parameters
4. Preview
5. Generate
```

但第一版也可以用 dock widget 完成。

重点是功能，不要过早花大量时间美化 UI。

------

# 50. 导出 CSV

支持：

```text_6fS8_0
CSV
```

格式：

```text_6fS8_0
segment,
point,
x,
y,
z,
nx,
ny,
nz,
qx,
qy,
qz,
qw
```

例如：

```csv_gqD1_2
segment,point,x,y,z,nx,ny,nz,qx,qy,qz,qw
0,0,120.1,52.3,31.8,0.1,0.2,0.974,0.0,0.1,0.0,0.995
```

同时提供：

```text_6fS8_0
X,Y,Z,Nx,Ny,Nz
```

简化导出格式。

------

# 51. ABB / RoboDK 扩展接口

当前不要让 ABB RobotStudio 或 RoboDK 成为依赖。

只定义：

```cpp_ZBwg_1
class RobotPathExporter
```

以后可以增加：

```text_6fS8_0
ABB RAPID
KUKA KRL
FANUC LS
Yaskawa INFORM
RoboDK CSV
```

第一版本实现：

```text_6fS8_0
Generic CSV
```

即可。

------

# 52. 线程

曲面路径生成可能耗时。

不能阻塞 Qt GUI。

使用：

```text_6fS8_0
QThread
QtConcurrent
```

或者适合的 worker architecture。

点击 Preview 后显示：

```text_6fS8_0
Generating path...
```

并提供：

```text_6fS8_0
Cancel
```

不能在 worker thread 直接操作：

```text_6fS8_0
AIS_InteractiveContext
V3d_View
QWidget
```

几何计算在 worker thread。

计算完成后回主线程更新显示。

------

# 53. Undo / Redo

架构中预留：

```text_6fS8_0
QUndoStack
```

至少为以下操作预留 command：

```text_6fS8_0
Select Surface
Create Path
Delete Path
Change Parameters
```

第一版本可以不全部实现，但不要把数据结构写死。

------

# 54. Numerical Tolerances

创建统一：

```cpp_ZBwg_1
struct GeometryTolerance
{
    double linear = 1e-4;
    double angular = ...;
    double join = 0.05;
};
```

不要在各个 cpp 文件中到处出现：

```cpp_ZBwg_1
1e-7
1e-5
0.01
```

魔法数字。

------

# 55. 错误处理

以下情况必须有明确 UI 提示：

```text_6fS8_0
STEP load failed
No face selected
Invalid selected geometry
Step over <= 0
Point distance <= 0
Invalid path direction
Path direction parallel to normal
No intersection curves found
Surface normal undefined
Degenerated curve
Degenerated face
Path contains no valid points
```

不要 crash。

------

# 56. STEP Heal

创建接口：

```cpp_ZBwg_1
CadHealer
```

第一版本至少可以检测：

```text_6fS8_0
Shape valid / invalid
```

可使用：

```text_6fS8_0
BRepCheck_Analyzer
```

以后可以加入：

```text_6fS8_0
ShapeFix_Shape
```

UI 可以显示：

```text_6fS8_0
CAD Status: Valid
```

或者：

```text_6fS8_0
CAD Status: Contains topology errors
```

------

# 57. Geometry Debug Panel

开发阶段非常需要。

增加 Debug 可视化：

```text_6fS8_0
Show Face IDs
Show Section Planes
Show Section Curves
Show UV
Show Reference Frame
Show Surface Normals
Show Raw Intersections
```

这对排查自由曲面路径错误非常重要。

------

# 58. 单元测试：平面

创建一个矩形：

```text_6fS8_0
100 x 80 mm
```

Step Over：

```text_6fS8_0
10 mm
```

期望大约产生：

```text_6fS8_0
9 条左右 section paths
```

视边界包含规则而定。

所有点应该：

```text_6fS8_0
Z = constant
```

法向一致。

------

# 59. 单元测试：圆柱

创建圆柱 Face。

选择圆柱侧面。

生成 parallel section path。

验证：

```text_6fS8_0
每一个 PathPoint
```

都位于圆柱真实 analytic surface 上。

验证 normal：

```text_6fS8_0
normal
```

与：

```text_6fS8_0
radial direction
```

一致。

------

# 60. 单元测试：球面

创建 sphere。

选择部分 trimmed spherical face。

确认：

- 路径被 trim boundary 正确裁剪
- 路径点位于 sphere
- normal 指向 radial direction
- 不生成 trim 外路径

------

# 61. 单元测试：BSpline Surface

程序创建一个起伏自由曲面，例如：

```text_6fS8_0
~___~~---___~~
```

转换为：

```text_6fS8_0
BSpline Surface
```

然后：

```text_6fS8_0
trim
```

成一个 Face。

测试 section intersection。

要求：

```text_6fS8_0
Path points are on selected B-Rep face
```

而不是 triangulation approximation。

------

# 62. 单元测试：Multi Face

创建两个相邻 Face：

```text_6fS8_0
Face A | Face B
```

同时选中。

section curve 跨越两个 Face。

验证：

```text_6fS8_0
Face ID
normal
curve ordering
continuity
```

都合理。

------

# 63. 单元测试：Hole

创建：

```text_6fS8_0
rectangular face
+
circular inner hole
```

生成路径。

任何 process path 都不能穿过 hole。

------

# 64. 单元测试：Reversed Face

创建：

```text_6fS8_0
TopoDS_Face
```

然后反转 orientation。

验证最终 tool surface normal 根据 Face orientation 正确翻转。

------

# 65. 性能

典型 STEP：

```text_6fS8_0
几百 ~ 几千 Faces
```

Selected region：

```text_6fS8_0
1 ~ 100 Faces
```

Path points：

```text_6fS8_0
1000 ~ 100000
```

UI 仍需要保持可操作。

不要每次鼠标移动都重新进行全部 BRep intersection。

缓存：

```text_6fS8_0
Face IDs
Bounding boxes
Selected Compound
Surface metadata
```

------

# 66. 第一阶段 UI

建议界面：

```text_6fS8_0
+-----------------------------------------------------------+
| File  View  Surface  Path  Tools                          |
+----------------+----------------------------+-------------+
|                |                            |             |
| Project Tree   |                            | Parameters  |
|                |       3D CAD Viewer        |             |
| CAD            |                            | Surface     |
| Selections     |                            | Direction   |
| Paths          |                            | Step Over   |
|                |                            | Spacing     |
|                |                            | Angles      |
|                |                            |             |
+----------------+----------------------------+-------------+
| Status / Progress                                           |
+-------------------------------------------------------------+
```

使用：

```text_6fS8_0
QMainWindow
QDockWidget
QTreeWidget/QTreeView
QToolBar
QStatusBar
```

------

# 67. Toolbar

提供：

```text_6fS8_0
Open STEP
Fit All
Select Face
Select Edge
Clear Selection
Select Tangent Faces
Preview Path
Generate Path
Delete Path
Export
```

------

# 68. 交互状态机

不要让：

```text_6fS8_0
Select Face
Select Edge
Pick Direction
Orbit
```

互相冲突。

定义：

```cpp_ZBwg_1
enum class InteractionMode
{
    Navigate,
    SelectFace,
    SelectEdge,
    PickDirectionPoint1,
    PickDirectionPoint2
};
```

所有鼠标操作依据：

```text_6fS8_0
InteractionMode
```

处理。

------

# 69. 状态栏

例如：

```text_6fS8_0
Face 18 | BSpline Surface | Area: 952.3 mm²
```

或者：

```text_6fS8_0
5 faces selected | Area: 3521.7 mm²
```

Path 完成后：

```text_6fS8_0
23 paths | 4281 points | Process length: 5328 mm
```

------

# 70. 不要犯的错误

严禁用下面方式作为核心：

```text_6fS8_0
STEP
↓
STL
↓
triangles
↓
raycast
↓
toolpath
```

这是我之前做过的思路，现在不要这样做。

正确架构：

```text_6fS8_0
STEP
↓
OCCT B-Rep
↓
TopoDS_Face
↓
Geom_Surface / trimmed Face
↓
B-Rep Surface Intersection
↓
3D Curve
↓
Arc-length Sampling
↓
Surface UV
↓
Exact CAD Surface Normal
↓
TCP Pose
```

------

# 71. 不允许使用 XY 假设

不能假设工件曲面位于：

```text_6fS8_0
XY Plane
```

不能把：

```text_6fS8_0
World Z
```

永远视为 surface normal。

所有算法必须支持：

```text_6fS8_0
任意位置
任意朝向
任意 STEP 曲面
```

------

# 72. 不允许把 UV 当 mm

非常重要：

```text_6fS8_0
U = 0.1
V = 0.1
```

不是：

```text_6fS8_0
0.1 mm
```

所以距离相关参数：

```text_6fS8_0
Step Over
Point Distance
Boundary Margin
Z Offset
```

都必须使用 3D 几何空间 mm 计算。

------

# 73. 不允许只支持单一曲面类型

不能写成：

```cpp_ZBwg_1
if Plane ...
else if Cylinder ...
else fail;
```

基本解析曲面可以做优化。

但核心必须至少可以处理：

```text_6fS8_0
Geom_BSplineSurface
```

因为工业 STEP 自由曲面大量使用 NURBS/B-Spline。

------

# 74. CMake

提供真正可以使用的 CMake。

类似：

```cmake_nJxb_3
find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    OpenGLWidgets
    Concurrent
)

find_package(OpenCASCADE REQUIRED)
```

但 OpenCASCADE 不同安装方式导出的 CMake target 有差异。

请根据当前开发环境实际调整。

不要生成一个形式正确但实际上完全链接不了 OCCT 的 CMakeLists。

README 说明：

```text_6fS8_0
Qt path
OCCT path
CMAKE_PREFIX_PATH
OpenCASCADE_DIR
```

如何配置。

------

# 75. Windows 优先

第一阶段优先保证：

```text_6fS8_0
Windows 10/11
MSVC 2022
Qt 6
OCCT
CMake
```

可编译运行。

同时代码尽量保持跨平台。

不要使用 MFC。

------

# 76. 第一阶段验收标准

启动软件。

点击：

```text_6fS8_0
Open STEP
```

选择：

```text_6fS8_0
part.stp
```

模型显示在 3D Viewer。

点击：

```text_6fS8_0
Select Face
```

鼠标点自由曲面。

选中曲面高亮。

Ctrl 可以多选。

设置：

```text_6fS8_0
Direction = Global X
Machining Angle = 30°
Step Over = 10 mm
Point Distance = 2 mm
Pattern = Zig-Zag
Tool Z = Against Surface Normal
```

点击：

```text_6fS8_0
Preview
```

必须直接在选中的曲面上看到多条路径。

曲面弯曲时：

```text_6fS8_0
路径也随 CAD 曲面弯曲
```

不是平面线。

每个轨迹点：

```text_6fS8_0
P
```

必须在所选择的 B-Rep Face 上。

每个轨迹点具有：

```text_6fS8_0
Surface Normal
Path Tangent
TCP Orientation
```

打开：

```text_6fS8_0
Show TCP Frames
```

能够看到 Tool Z 随曲面 normal 连续变化。

点击：

```text_6fS8_0
Generate
```

路径进入 Project Tree。

点击：

```text_6fS8_0
Export CSV
```

正确输出：

```text_6fS8_0
XYZ
Normal
Quaternion
Face ID
Segment ID
Point ID
```

------

# 77. 开发实施顺序

不要一开始就做所有高级功能。

严格按照下面顺序实现。

## Milestone 1

完成：

```text_6fS8_0
Qt MainWindow
+
OCCT Viewer
+
STEP import
```

确认真实 STEP 模型能够显示。

## Milestone 2

完成：

```text_6fS8_0
Face selection
+
Face highlighting
+
Selected Face retrieval
```

确认鼠标点击后真的拿到：

```cpp_ZBwg_1
TopoDS_Face
```

打印：

```text_6fS8_0
Face ID
Surface Type
Area
```

## Milestone 3

完成：

```text_6fS8_0
Single selected Face
+
Single section plane
+
BRep intersection
```

把 section curve 显示出来。

这是最重要的算法验证点。

## Milestone 4

完成：

```text_6fS8_0
Parallel section planes
+
Step Over
+
Multiple curves
```

生成完整 surface raster。

## Milestone 5

完成：

```text_6fS8_0
Curve arc-length sampling
+
Surface UV
+
Exact surface normal
```

## Milestone 6

完成：

```text_6fS8_0
Zig-Zag ordering
+
Segments
+
Holes
+
Multi Face
```

## Milestone 7

完成：

```text_6fS8_0
TCP orientation
+
Lead/Tilt
+
Tool Offset
```

## Milestone 8

完成：

```text_6fS8_0
Parameter UI
+
Preview
+
Project Tree
```

## Milestone 9

完成：

```text_6fS8_0
CSV export
+
tests
+
README
```

不要跳过前面的几何验证直接做漂亮 UI。

------

# 78. Codex 工作要求

不要只给我：

```text_6fS8_0
架构建议
伪代码
示例片段
```

而是直接在当前 repository 中创建：

```text_6fS8_0
.h
.cpp
CMakeLists.txt
README
tests
```

实现可运行项目。

每完成一个 Milestone：

1. 编译项目
2. 修复 compiler error
3. 修复 linker error
4. 运行当前可执行程序/测试
5. 确认再进入下一 Milestone

不要在代码中留下大量：

```cpp_ZBwg_1
TODO
```

来假装功能已经实现。

如果一个高级功能当前无法可靠完成，应明确标记：

```text_6fS8_0
Not implemented
```

而不是写一个错误算法。

------

# 79. 最重要的工程原则

这个项目首先是：

```text_6fS8_0
CAD Surface Toolpath Generator
```

然后才是：

```text_6fS8_0
Robot Path Generator
```

几何正确性优先级：

```text_6fS8_0
B-Rep Face selection
>
correct surface intersection
>
correct path sampling
>
correct surface normal
>
correct TCP orientation
>
robot export
>
UI polish
```

绝对不要为了快速得到视觉效果而退回 STL ray projection。

------

# 80. 最终测试数据

如果 repository 中没有测试 STEP：

请用 OpenCASCADE 在 test/debug 程序中程序化创建以下测试模型：

```text_6fS8_0
Plane
Cylinder
Sphere
Trimmed BSpline Surface
Two connected curved Faces
Surface with inner hole
```

可以保存成：

```text_6fS8_0
.step
```

作为 examples。

然后用这些模型实际验证：

```text_6fS8_0
Face selection
Section path
Normal
Trim
Hole
Multi Face
TCP frame
```

确保核心算法不是只对某一个用户 STEP 文件有效。

完成后在 README 中加入：

```text_6fS8_0
Current capabilities
Known limitations
Algorithm definition
Build instructions
Usage workflow
Coordinate conventions
TCP frame definition
Step-over definition
Future geodesic planner
```

最终目标是获得一个可以继续扩展成工业机器人曲面加工离线编程软件的可靠基础，而不是一个单文件 demo。

刘俊杰17860730750

![img](icon.gif)