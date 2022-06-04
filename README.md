# RoutineArranger

**NOTE: This project is Chinese only, and is primarily a homework not intended for practical use.**

*完成于 2022/5/9*

#### 简介

这是一份**课程设计作业**，主要功能是进行非常简单的日程管理。本程序使用的第三方库为基于 C++17 的 [C++/WinRT](https://docs.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/intro-to-using-cpp-with-winrt)，因此建议使用自带此库的 Visual Studio 2022 进行编译。本程序主要通过 [XAML Island](https://docs.microsoft.com/en-us/windows/apps/desktop/modernize/xaml-islands) 提供基于 Windows 10 风格的用户界面，因此必须在 Windows 10 1903+ 上运行，并会具有部分由操作系统导致的 BUG。

#### 模块

本程序主要分为两个模块：核心模型(`RoutineArranger_Core.*`)与 UI 呈现(`RoutineArranger_UI.*`)，整体设计上部分应用了 MVP 架构。`json.*`用于借助 Windows 自带的 JSON API 实现符合 C++ 使用习惯的 JSON 模块。`util.*`用于将部分代码编写中常用的功能封装为便于调用的模块。`windowing.*`用于提供便于与 XAML 控件对接的窗口模块。

#### 目前存在的缺陷

* 由于目前能力的不足，未能在本程序中应用 MVVM，从而导致 UI 呈现部分的代码耦合度过高。
* 核心模型模块与 Windows 提供的 API 有一定的耦合，应当适当解耦。
* 当前的日程算法不够高效(核心模型 + UI 呈现)，应当适当采用更好的算法、数据虚拟化等技术来提高用户体验。
* 由于时间原因，部分界面上的细节(如动画、布局、提示等交互体验)未能全部完善，需要对此进行更细致的打磨。
* 部分原本预期实现的功能未能全部实现，需要在将来做出更细致的规划与调研。
* 本程序对操作系统过于挑剔，这也许需要通过使用更加成熟的第三方库(如 Qt)实现用户界面来解决。