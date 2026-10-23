# SDK 本地布局与图片反馈

SDK 提供布局加载和控件反馈接口。业务动作通过控件名称更新数据，SDK 在本地合成 PNG，
再使用现有 `setImage` 协议发送给上位机。上位机无需解析布局 JSON，也无需处理反馈布局事件。

图片绘制使用 Windows/macOS 系统库，不依赖 Qt Gui、Qt Svg 或窗口平台插件。
Qt Core 和 WebSockets 仍用于 JSON、资源读取、事件循环及通信。

## 接口用法

在动作出现或需要切换布局时加载 JSON：

```cpp
SetFeedbackLayout("layouts/Parameter.json");
```

文件路径相对于插件根目录；也可以使用绝对路径或 Qt 资源路径：

```cpp
SetFeedbackLayout(":/layouts/Parameter.json", QSize(200, 100));
```

更新控件数据：

```cpp
SetFeedback({
    {"title", "透明度"},
    {"icon", "images/parameter.png"},
    {"value", "75%"},
    {"indicator", 75}
});
```

未指定的控件保持原有值。属性对象递归合并，可以独立修改可见性、透明度或字体：

```cpp
SetFeedback({
    {"value", "80%"},
    {"indicator", QJsonObject{{"value", 80}, {"enabled", true}}},
    {"title", QJsonObject{{"font", QJsonObject{{"size", 18}}}, {"opacity", 0.5}}}
});
```

`SetFeedbackLayout` 还有一个接收 `QJsonObject` 的重载，第三个参数指定图片资源根目录：

```cpp
SetFeedbackLayout(layoutObject, QSize(200, 100), pluginDirectory);
```

所有布局与控件状态按动作实例独立保存。重新加载布局会恢复默认值，并立即绘制。
布局数据未变化时复用已渲染图片；图片内容相同则不重复发送。
`SetFeedback(payload, true)` 可以强制重新发送当前图片。

## 布局示例

```json
{
  "id": "Parameter",
  "background": "#102333",
  "items": [
    {
      "key": "title", "type": "text", "rect": [12, 6, 176, 26],
      "font": { "size": 16, "weight": 600 },
      "alignment": "left", "text-overflow": "ellipsis", "value": ""
    },
    {
      "key": "icon", "type": "pixmap", "rect": [12, 37, 48, 48], "value": ""
    },
    {
      "key": "value", "type": "text", "rect": [72, 36, 116, 34],
      "font": { "size": 24, "weight": 600 },
      "alignment": "right", "text-overflow": "ellipsis", "value": ""
    },
    {
      "key": "indicator", "type": "bar", "rect": [72, 76, 116, 10],
      "range": { "min": 0, "max": 100 }, "border_w": 0,
      "bar_bg_c": "#29485f", "bar_fill_c": "#31a8ff", "value": 0
    }
  ]
}
```

默认逻辑画布为 200×100。顶层 `size: [宽, 高]` 可以指定其他逻辑尺寸，
`background` 指定底色，默认黑色。`SetFeedbackLayout` 的 QSize 参数控制输出图片尺寸，
每条边限制在 72–1024 像素；控件坐标随画布缩放。

还可以在 manifest 的动作定义中声明默认布局：

```json
"Encoder": { "layout": "layouts/Parameter.json" }
```

首次调用 `SetFeedback` 时，SDK 自动加载当前动作 UUID 对应的默认布局。
显式调用 `SetFeedbackLayout` 可以切换布局或调整输出尺寸。
manifest 和相对图片路径的根目录从程序目录向上寻找 `manifest.json`。
使用资源布局时，相对图片路径从 Qt 资源根目录读取。

## 支持范围

| 控件       | 功能                                                                   |
| ---------- | ---------------------------------------------------------------------- |
| `text`   | 文字、字号、常规/粗体、左/中/右对齐、颜色、clip/ellipsis 截断或 fit 自动缩小 |
| `pixmap` | PNG/JPEG 等原生位图、base64 位图 data URL、Qt 资源图片，保持宽高比居中 |
| `bar`    | 数值范围、填充进度、背景色、填充色、边框色及边框宽度                   |
| `gbar`   | 全宽颜色条、下方三角指示器、数值范围及`bar_h`                        |

所有控件支持 `enabled`、`opacity` 和 `zOrder`，按层级稳定排序。
`key`、`type`、`rect` 在加载布局时确定，反馈更新不能修改它们。
文字字重以 600 为分界映射常规/粗体；也接受字符串 `bold`、`normal`。

颜色支持 `#RGB`、`#RRGGBB`、`#AARRGGBB` 和源码 `color()` 列出的常用名称。
控件背景和条形填充支持水平渐变，例如 `0:#ff0000,0.5:#ffff00,1:#00ff00`。
画布背景目前仅支持单色。

图片路径省略扩展名时尝试 `.png`、`.jpg`、`.jpeg`。SVG 文件路径会改读同名 PNG，
需要开发阶段准备好位图资源；运行时不解析 SVG，也不支持 SVG data URL。
布局内置名称、fade 截断、条形 subtype 1/2/3、宿主字体设置覆盖和 manifest 背景图片
尚未实现。subtype 0/4 均使用矩形条绘制。两端使用系统字体与抗锯齿，像素结果可能不同。

布局加载及反馈接口返回 bool。无效 JSON、重复控件名称、未知反馈 key、错误数据类型或
不支持的配置会返回 false，并写入 `[FeedbackLayout]` 日志。
无效布局或反馈不会覆盖已有状态；原生绘制失败时保留已接受的数据，便于再次尝试。
存在 `title` 控件时，`SetTitle` 优先更新本地控件；若绘图失败则回退到普通标题协议。
本地图片使用 target 0，更新硬件与软件显示。加载布局时会清除宿主标题，避免重复叠加。

## 集成系统库

使用 qmake 时包含 `SDK.pri`，它会自动选择对应平台的绘图源码和链接库。
使用 CMake 时，将下列源码加入 SDK 目标：

```cmake
target_sources(ProjectQtSDK PRIVATE SDK/KnobFeedback.cpp SDK/KnobFeedback.h)
if(WIN32)
    target_sources(ProjectQtSDK PRIVATE SDK/KnobFeedbackWindows.cpp)
    target_compile_definitions(ProjectQtSDK PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ProjectQtSDK PRIVATE gdiplus ole32)
elseif(APPLE)
    target_sources(ProjectQtSDK PRIVATE SDK/KnobFeedbackMac.cpp)
    target_link_libraries(ProjectQtSDK PRIVATE
        "-framework CoreGraphics" "-framework CoreText"
        "-framework ImageIO" "-framework CoreFoundation")
endif()
```

Windows 使用 GDI+ 绘图、内存 IStream 编码 PNG；macOS 使用 CoreGraphics、CoreText、ImageIO。
程序入口使用 `QCoreApplication` 即可。其他平台的原生图片渲染暂未实现。
SDK 还修复了 `dialUp` 事件分发中的判断错误，旋钮松开现在进入对应处理函数。

上位机仍需发送旋钮输入事件，并将该 context 的 `setImage` 显示到对应区域。
本次只提交 SDK 接口、渲染后端、SDK.pri 和本说明；应用接入、图片及布局资源由插件项目准备。
未编译或进行设备实测，需在允许编译后分别验证 Windows/macOS 的显示和增量更新。
