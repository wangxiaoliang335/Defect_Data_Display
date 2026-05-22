# Defect_Data_Display 部署说明

## 快速部署

1. **编译项目**
   - 在 Visual Studio 中编译项目

2. **运行部署脚本**
   - 双击 `deploy.bat`
   - 脚本会自动：
     - 复制程序和 Qt 依赖库
     - 复制 VC++ 运行库
     - 创建压缩包

3. **复制部署文件夹到目标机器**
   ```
   deployment/
   ├── Defect_Data_Display.exe
   ├── Qt6/
   ├── platforms/
   └── vcredist_x64/
   ```

## 目标机器要求

### 1. MySQL ODBC 驱动（必须）
下载并安装：https://dev.mysql.com/downloads/connector/odbc/

选择：**Windows (x86, 64-bit), MSI Installer**

### 2. 操作系统
- Windows 10 / Windows 11 x64

## 部署结构说明

| 文件/文件夹 | 说明 |
|------------|------|
| `Defect_Data_Display.exe` | 主程序 |
| `Qt6/` | Qt 运行时库 |
| `platforms/` | Windows 平台插件 |
| `vcredist_x64/` | VC++ 运行库 |
| `mysql/`, `odbc32.dll` | MySQL ODBC 驱动（如需要）|

## 常见问题

### Q: 提示缺少 msvcp140.dll
A: 安装 VC++ 2015-2022 运行库
   https://aka.ms/vs/17/release/vc_redist.x64.exe

### Q: 提示数据库连接失败
A: 1. 确认目标机器已安装 MySQL ODBC 5.3 Driver
   2. 检查 MySQL 服务是否运行
   3. 检查防火墙设置

### Q: 界面显示异常
A: 确保复制了完整的 `platforms/` 文件夹

## 数据库配置

程序连接参数：
- Driver: MySQL ODBC 5.3 ANSI Driver
- Server: localhost
- Port: 3306
- Database: ivs_lcd
- UID: root
- PWD: 123456

如需修改，编辑 `Defect_Data_Display.cpp` 中的连接字符串，然后重新部署。
