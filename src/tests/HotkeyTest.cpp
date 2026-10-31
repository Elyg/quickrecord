#include "core/Hotkey.h"

#include <gtest/gtest.h>

using namespace qr;

TEST(Hotkey, SuperShiftR)
{
    const auto hk = toX11Hotkey(QKeySequence("Meta+Shift+R"));
    ASSERT_TRUE(hk);
    EXPECT_EQ(hk->keysym, uint32_t('r'));
    EXPECT_EQ(hk->modifiers, kModSuper | kModShift);
}

TEST(Hotkey, FunctionAndPrintKeys)
{
    EXPECT_EQ(toX11Hotkey(QKeySequence("Ctrl+F9"))->keysym, 0xffc6u);
    EXPECT_EQ(toX11Hotkey(QKeySequence("Ctrl+F9"))->modifiers, kModControl);
    EXPECT_EQ(toX11Hotkey(QKeySequence("Alt+Print"))->keysym, 0xff61u);
    EXPECT_EQ(toX11Hotkey(QKeySequence("Alt+Print"))->modifiers, kModAlt);
    EXPECT_EQ(toX11Hotkey(QKeySequence("Ctrl+5"))->keysym, uint32_t('5'));
}

TEST(Hotkey, RejectsEmptyAndUnknown)
{
    EXPECT_FALSE(toX11Hotkey(QKeySequence()));
    EXPECT_FALSE(toX11Hotkey(QKeySequence(Qt::Key_VolumeUp)));
}

TEST(Hotkey, MacKeyCodesAndModifiers)
{
    const auto hk = toMacHotkey(QKeySequence("Meta+Shift+R"));
    ASSERT_TRUE(hk);
    EXPECT_EQ(hk->keyCode, 0x0Fu); // kVK_ANSI_R
    EXPECT_EQ(hk->modifiers, kMacControl | kMacShift);
    EXPECT_EQ(toMacHotkey(QKeySequence("Ctrl+Alt+5"))->keyCode, 0x17u); // kVK_ANSI_5
    EXPECT_EQ(toMacHotkey(QKeySequence("Ctrl+Alt+5"))->modifiers, kMacCmd | kMacOption);
    EXPECT_EQ(toMacHotkey(QKeySequence("F12"))->keyCode, 0x6Fu);
    EXPECT_EQ(toMacHotkey(QKeySequence("Print"))->keyCode, 0x69u); // F13
    EXPECT_FALSE(toMacHotkey(QKeySequence(Qt::Key_VolumeUp)));
}

TEST(Hotkey, EscapeMapsOnBothPlatforms)
{
    EXPECT_EQ(toX11Hotkey(QKeySequence(Qt::Key_Escape))->keysym, 0xff1bu);
    EXPECT_EQ(toX11Hotkey(QKeySequence(Qt::Key_Escape))->modifiers, 0u);
    EXPECT_EQ(toMacHotkey(QKeySequence(Qt::Key_Escape))->keyCode, 0x35u); // kVK_Escape
    EXPECT_EQ(toMacHotkey(QKeySequence(Qt::Key_Escape))->modifiers, 0u);
}

TEST(Hotkey, WinKeysAndModifiers)
{
    const auto hk = toWinHotkey(QKeySequence("Ctrl+Alt+Shift+R"));
    ASSERT_TRUE(hk);
    EXPECT_EQ(hk->virtualKey, uint32_t('R'));
    EXPECT_EQ(hk->modifiers, kWinControl | kWinAlt | kWinShift);

    EXPECT_EQ(toWinHotkey(QKeySequence("Meta+7"))->virtualKey, uint32_t('7'));
    EXPECT_EQ(toWinHotkey(QKeySequence("Meta+7"))->modifiers, kWinMeta);
    EXPECT_EQ(toWinHotkey(QKeySequence("F1"))->virtualKey, 0x70u);
    EXPECT_EQ(toWinHotkey(QKeySequence("F24"))->virtualKey, 0x87u);
    EXPECT_EQ(toWinHotkey(QKeySequence("Print"))->virtualKey, 0x2Cu);
    EXPECT_EQ(toWinHotkey(QKeySequence("Esc"))->virtualKey, 0x1Bu);
    EXPECT_EQ(toWinHotkey(QKeySequence("Esc"))->modifiers, 0u);
    EXPECT_FALSE(toWinHotkey(QKeySequence()));
    EXPECT_FALSE(toWinHotkey(QKeySequence("Ctrl+F30")));
}
