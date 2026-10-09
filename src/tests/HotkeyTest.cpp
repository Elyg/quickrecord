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
