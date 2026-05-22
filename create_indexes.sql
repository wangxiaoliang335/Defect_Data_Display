-- =====================================================
-- Database Index Optimization Script for ivs_lcd Database
-- Run this script in MySQL to create necessary indexes
-- =====================================================

USE ivs_lcd;

-- 为 ivs_lcd_inspectionresult 表添加 StartTime 索引
-- 这是最重要的索引，因为所有查询都基于 StartTime 过滤
CREATE INDEX IDX_StartTime ON ivs_lcd_inspectionresult(StartTime);

-- 创建复合索引用于统计查询（包含 AOIResult 用于 pass/fail 统计）
CREATE INDEX IDX_StartTime_AOIResult ON ivs_lcd_inspectionresult(StartTime, AOIResult);

-- 创建复合索引用于平台统计查询
CREATE INDEX IDX_StartTime_PlatformID ON ivs_lcd_inspectionresult(StartTime, PlatformID, AOIResult);

-- 为 ivs_lcd_aoidefect 表添加 StartTime 索引
CREATE INDEX IDX_StartTime ON ivs_lcd_aoidefect(StartTime);

-- 创建复合索引用于按类型统计
CREATE INDEX IDX_StartTime_Type ON ivs_lcd_aoidefect(StartTime, Type);

-- 验证索引创建成功
SELECT '=== ivs_lcd_inspectionresult indexes ===' as Info;
SHOW INDEX FROM ivs_lcd_inspectionresult;

SELECT '=== ivs_lcd_aoidefect indexes ===' as Info;
SHOW INDEX FROM ivs_lcd_aoidefect;
