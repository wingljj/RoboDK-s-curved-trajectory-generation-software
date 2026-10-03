# 上海卫星装备研究所曲面轨迹生成软件 0.1.2

研发单位：上海卫星装备研究所。研发部门：总装中心。使用用户提供的中国航天标志。
软件“帮助 → 关于”提供功能简介和版本信息。安装向导、快捷方式和卸载信息采用同一名称与图标。

Windows x64 安装包由 Inno Setup 构建，说明与复现命令见 [安装与打包](docs/installation.md)。
当前品牌版本的独立运行目录为 `dist-branding-qt5` 和 `dist-branding-qt6`。

独立的 C++17 / Qt Widgets / Open CASCADE 桌面 CAD 曲面路径软件。
输入 STEP/STP，直接选择真正的 `TopoDS_Face`，通过修剪 B-Rep 与平行平面
求交生成曲面路径，再计算 CAD 法向与 TCP 姿态。显示允许 OCCT 三角化；
路径计算不读取三角网格。无需 ABB、RoboDK、Python、VTK 或 Open3D。

本机可直接双击 `dist-branding-qt5/SurfacePathStudio.exe`（Qt 5.15.0），
或 `dist-branding-qt6/SurfacePathStudio.exe`（Qt 6.5.3）。请保留同目录 DLL 和 `platforms` 文件夹。
两套 Release 构建均通过 42 项 CTest 与桌面全流程验收，记录在 `docs/validation.md`。
保留生成后的轨迹/法向默认显示、连续 CSV 和拖入 STP/STEP。

## 当前能力

- STEP/XCAF 导入：装配层次、名称、颜色、B-Rep 拓扑、文件单位换算为 mm，
  中文路径读取，拓扑有效性检测。
- 原生 OCCT Viewer：旋转、平移、缩放、适合窗口、七个标准视图、着色/线框、模型显隐。
- 鼠标 Face 选择与高亮、Ctrl 多选/取消、Esc 清空、稳定运行时 Face ID、
  曲面类型与面积；共边相切 Face 扩选。
- Global X/Y/Z、CAD Edge 中点切线、两个曲面拾取点、自定义向量，加工角度。
- 修剪面精确截线、孔洞/岛保留分段、多 Face 处理，满足共边及平滑条件的跨面连接。
- 真实三维弧长采样、OCCT 曲率采样与弦误差/法向角度细分、UV 与 Face 内部分类。
- Zig-Zag/One-Way、起始侧/首行方向、三维边界余量、切向 Approach/Depart 延长。
- 曲面正/反法向 Tool Z、Lead/Tilt、保留接触点的 TCP 偏置、连续四元数符号。
- 后台几何计算与取消，临时 Preview 与项目树 Generate，多个保存路径及显隐、
  重命名、删除、重算；选择和路径命令使用 `QUndoStack`。
- 路径、接触点、法向、TCP 坐标系、参考坐标系、切片平面、Face ID 与层号显示开关。
- 完整 CSV 与简化 XYZ+Normal 导出，以及七个程序化 OCCT STEP 示例。
- 本地 STP/STEP 拖放导入，支持中文路径和大小写扩展名；轨迹行间直线 Transfer 连接。

## 构建

实际开发机：Windows x64、MSVC 2019/v142、Qt 6.5.3、OCCT 7.6.0、CMake 3.29、Ninja。
MSVC 2022 提供配置预设；不同 Qt/OCCT 必须使用兼容的编译器、架构和运行库。
代码同时处理 Qt 5/6 鼠标和字符串 API。

### 当前电脑

在项目目录打开 PowerShell：

```powershell
./scripts/Build.ps1 -RunTests -Deploy
./dist/SurfacePathStudio.exe
```

Qt 5 构建与独立交付目录：

```powershell
./scripts/Build.ps1 -QtRoot 'D:/Software/QT/content/5.15.0/msvc2019_64' `
  -BuildName native-qt5 -RunTests -Deploy -DeployDirectory './dist-qt5'
./dist-qt5/SurfacePathStudio.exe
```

脚本显式使用本机已安装的编译器和 Qt，避免误用 PATH 中的 MinGW/Anaconda Qt。
它复用 `E:/surfacepath-poc-20261002/.deps/occt-static-build` 的纯 OCCT 静态 SDK，
并从匹配的官方源码补编缺失的 `TKOpenGl`。软件不链接旧 PoC 的插件或 RoboDK API。
脚本仅在子进程初始化 MSVC 环境；中文工程路径通过经过目标校验的 ASCII junction 构建。

```powershell
./scripts/Build.ps1 `
  -QtRoot 'D:/Software/QT/content/6.5.3/msvc2019_64' `
  -VcVars64 'D:/Software/visual2019/content/VC/Auxiliary/Build/vcvars64.bat' `
  -OcctInclude 'E:/surfacepath-poc-20261002/.deps/occt-static-build/inc' `
  -OcctLib 'E:/surfacepath-poc-20261002/.deps/occt-static-build/win64/vc14/lib' `
  -OcctSource 'E:/surfacepath-poc-20261002/.deps/OCCT-7_6_0'
```

### 完整 Qt/OCCT SDK

在 VS x64 Native Tools 环境运行。`OpenCASCADE_DIR` 指向包含
`OpenCASCADEConfig.cmake` 的目录；`CMAKE_PREFIX_PATH` 指向 Qt 安装根。
SDK 必须包括 Visualization、ApplicationFramework 与 DataExchange。

```powershell
cmake -S . -B build/sdk -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='C:/Qt/6.5.3/msvc2019_64' `
  -DOpenCASCADE_DIR='C:/OCCT/cmake'
cmake --build build/sdk --parallel 8
ctest --test-dir build/sdk --output-on-failure
```

也可使用 `CMakePresets.json` 的 `msvc2022` 预设，并通过
`CMakeUserPresets.json` 或命令行设置本机 SDK 路径。若 SDK 的 CMake package
不完整，可显式配置 `SURFACEPATH_OCCT_INCLUDE_DIR`、
`SURFACEPATH_OCCT_LIBRARY_DIR`，并为静态库设置 `SURFACEPATH_OCCT_STATIC=ON`。
缺少 `TKOpenGl` 时可设置匹配版本的 `SURFACEPATH_OCCT_SOURCE_DIR`；
它只补编该官方模块，不重写 OpenGL/CAD 内核。

## 使用流程

1. **打开 STEP**，将单个 **STP/STEP 文件拖入窗口**，或从 **示例** 菜单载入测试模型。
2. **选择 Face**，左键选择，Ctrl+左键增加或取消，Esc 清空。
3. 设置方向；选 Edge 后使用真实 CAD 曲线的中点切线；两点模式要求拾取模型表面。
4. 设置加工角度、截面间距、采样间距、Tool Z、Lead/Tilt 等。
5. 点击 **预览**，后台计算并显示临时路径；参数或区域变化后重新预览。
6. 点击 **生成** 保存到项目树；轨迹、加工点、法向和 TCP 坐标系默认可见，**视图** 菜单可单独开关。
7. 选中保存路径后 **导出 CSV**。路径树右键可重命名、删除、显隐和重新生成。

右键拖动旋转，中键拖动平移，滚轮缩放；导航模式中左键拖动也可旋转。
不能唯一确定法向的近封闭区域，需要填写自定义参考法向，并选择不与其平行的方向。
加工轨迹橙色（选中为黄色）、行间连接天蓝色、接近段绿色、离开段品红；曲面法向为青色。
拖入新的 CAD 会替换当前模型和路径；计算期间拒绝替换。拖入可落在视口、项目树或参数区。

## 算法与坐标约定

内部长度统一为 mm，使用 STEP 转换后的 CAD 世界坐标，支持任意刚性位置与朝向。
参考 `N` 是修剪区域的面积加权法向，仅用于建立切片坐标系。用户方向先投影到
参考切平面，再绕 `N` 旋转加工角度；`B=N×T`，重正交 `T=B×N`。
平面满足 `B·(X-C)=d_i`，每个平面通过 `BRepAlgoAPI_Section` 与 selected Face
compound 求交。路径从实际修剪 B-Rep 的截线取得，外边界、孔洞和岛自动参与求交。

**Step Over 是相邻切片平面的三维 Euclidean distance，不是曲面 geodesic distance。**
高倾斜或高曲率区域的实际沿面间距可以更大。起始侧决定切片从 Min B 或 Max B 锚定。
包围盒用于覆盖完整区域，因此倾斜工件或曲面可能存在没有截线的切片层。

采样并集 `GCPnts_UniformAbscissa` 弧长采样与 `GCPnts_TangentialDeflection`
曲率采样，再以半弧长递归验证四分之一、中点和四分之三处的弦误差和 CAD 法向角。
UV 通过 pcurve 或曲面投影取得，并通过 `BRepClass_FaceClassifier` 检查。
法向来自 CAD 一阶导数，并按 `TopAbs_REVERSED` 翻转。
当用户法向角容差大于 89° 时，采样和跨面连接保守使用 89° 上限，
防止粗采样或反向 patch 让连续性处理破坏正确的拓扑法向。

边界余量是到区域外边界和孔边界的 **3D shortest Euclidean distance**，
内部共边和 periodic seam 排除。当前为采样点过滤与分段，不是精确的曲面边界偏置。
加工段保持独立；CSV 的 `segment`/`motion_type` 保留真实加工与连接的区别。
相邻加工行按顺序增加直线 Transfer，连接前段 TCP 末点与后段 TCP 起点；有切向延长时
连接 Depart 与下一段 Approach。连接不计为沿面加工，也不包含避障/碰撞/安全高度规划。

基础 TCP：Z 沿或逆真实 CAD 法向；X 为路径切线在切平面的投影；Y=Z×X，
然后 X=Y×Z，形成右手系。Lead 绕局部 Y，随后 Tilt 绕旋转后的局部 X。
正 Z Offset 沿曲面法向，独立于 Tool Z 正反；保留原始 Contact Point。
四元数表示工具局部坐标到 CAD 世界坐标的主动旋转，顺序 **qx,qy,qz,qw**。
切向延长只外推 TCP，Contact Point 留在端点，并单独标为 Approach/Depart。

## CSV

完整格式（XYZ 为 TCP 点）：

```text
segment,point,x,y,z,nx,ny,nz,qx,qy,qz,qw,face_id,layer_id,motion_type,contact_x,contact_y,contact_z,u,v
```

简化格式 `X,Y,Z,Nx,Ny,Nz` 输出全部运动目标，保持加工及连接顺序。
完整和简化 CSV 均无段间空行；各加工段通过明确的 Transfer 目标连接。
推荐机器人后处理读取完整格式，以保留运动类型、曲面接触点与段边界。
没有实施任何品牌机器人程序、IK 或碰撞规划。

## 测试与示例

```powershell
./scripts/Build.ps1 -RunTests
./build/native-release/tests/surfacepath_generate_examples.exe ./examples/models
./scripts/SmokeTest.ps1
```

示例：100×80 平面、圆柱 patch、整圆柱、trimmed 球面、trimmed 起伏 BSpline、
两个共边 BSpline patch、圆形内孔平面。测试包含毫米/英寸/中文路径、嵌套装配颜色与位置、
STEP→B-Rep→路径→UV/姿态→CSV 全流程、法向反转、任意刚性变换、弧长/误差约束、
孔洞/岛、边界余量、Zig-Zag/One-Way、姿态与偏置、异常参数、取消和资源上限。
桌面 smoke 通过真实 AIS 拾取和 Qt 鼠标事件验证 Face/Ctrl/Edge/两点选择，以及后台计算、
项目树、Undo/Redo、默认法向/姿态及生成显示、连续 CSV；投递 Qt 拖放事件验证中文 STP
可落在视口/项目树/参数区，无效及计算中拖入被拒绝。保存 OCCT 渲染合成截图与 JSON 结果。

## 支持边界

未实现：严格 geodesic 等距策略、曲面约束延长、机器人品牌代码、碰撞/IK、
项目文件持久化、完整参数 Undo/Redo、UV 文本及 raw B-Rep intersection 独立调试层。
未来策略保持 `PathPlanner` 接口隔离。`SurfaceConstrained` 在 UI 禁用，API 明确报错。
当前复用的 no-FreeType OCCT SDK 可正常显示曲面、路径和坐标轴；AIS Face ID/层号文字
取决于 SDK 字体支持，状态栏与项目树中的 Face ID 始终可见。
交付目录包含 Qt、插件和 MSVC x64 Release 运行库；其他电脑尚未进行实机验收。
构建依赖仍需自行配置 Qt/OCCT SDK，MSVC 2022 预设尚未在本机运行。

源码调研见 [docs/research.md](docs/research.md)，验证记录见
[docs/validation.md](docs/validation.md)，第三方来源见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
