#include "core/KeyLabel.h"

#include <gtest/gtest.h>

using namespace qr;

namespace
{
KeyPress letter(const QString& text, const QString& base)
{
    KeyPress k;
    k.text = text;
    k.base = base;
    return k;
}

KeyPress special(SpecialKey s)
{
    KeyPress k;
    k.special = s;
    return k;
}
} // namespace

TEST(KeyLabel, PlainLettersAreTyped)
{
    const KeyLabel a = keyLabel(letter("a", "a"), MetaName::Super);
    EXPECT_EQ(a.label, "a");
    EXPECT_TRUE(a.typed);

    KeyPress shifted = letter("A", "a");
    shifted.shift = true; // Shift only chooses the character
    EXPECT_EQ(keyLabel(shifted, MetaName::Super).label, "A");
    EXPECT_TRUE(keyLabel(shifted, MetaName::Super).typed);

    EXPECT_EQ(keyLabel(letter("ä", "ä"), MetaName::Super).label, "ä");
}

TEST(KeyLabel, AltGrCharactersAreTyped)
{
    // AltGr isn't reported as Ctrl or Alt: the monitors leave both unset for it.
    const KeyLabel euro = keyLabel(letter("€", "e"), MetaName::Win);
    EXPECT_EQ(euro.label, "€");
    EXPECT_TRUE(euro.typed);
}

TEST(KeyLabel, SpaceIsTypedAsASpace)
{
    const KeyLabel space = keyLabel(special(SpecialKey::Space), MetaName::Super);
    EXPECT_EQ(space.label, " ");
    EXPECT_TRUE(space.typed);

    KeyPress ctrlSpace = special(SpecialKey::Space);
    ctrlSpace.ctrl = true;
    EXPECT_EQ(keyLabel(ctrlSpace, MetaName::Super).label, "Ctrl+Space");
    EXPECT_FALSE(keyLabel(ctrlSpace, MetaName::Super).typed);
}

TEST(KeyLabel, ShortcutsUseTheUnmodifiedCharacter)
{
    KeyPress k = letter("\x12", "r"); // Ctrl+Shift+R types a control character
    k.ctrl = true;
    k.shift = true;
    const KeyLabel l = keyLabel(k, MetaName::Super);
    EXPECT_EQ(l.label, "Ctrl+Shift+R");
    EXPECT_FALSE(l.typed);

    KeyPress digit = letter("!", "1");
    digit.ctrl = true;
    digit.shift = true;
    EXPECT_EQ(keyLabel(digit, MetaName::Super).label, "Ctrl+Shift+1");
}

TEST(KeyLabel, AltLetterIsAShortcut)
{
    KeyPress k = letter("f", "f");
    k.alt = true;
    EXPECT_EQ(keyLabel(k, MetaName::Win).label, "Alt+F");
    EXPECT_FALSE(keyLabel(k, MetaName::Win).typed);
}

TEST(KeyLabel, ModifiersInPcOrder)
{
    KeyPress k = letter("s", "s");
    k.ctrl = k.alt = k.shift = k.meta = true;
    EXPECT_EQ(keyLabel(k, MetaName::Super).label, "Ctrl+Alt+Shift+Super+S");
    EXPECT_EQ(keyLabel(k, MetaName::Win).label, "Ctrl+Alt+Shift+Win+S");
}

TEST(KeyLabel, SpecialKeysShowSymbols)
{
    EXPECT_EQ(keyLabel(special(SpecialKey::Return), MetaName::Super).label, "↩");
    EXPECT_FALSE(keyLabel(special(SpecialKey::Return), MetaName::Super).typed);
    EXPECT_EQ(keyLabel(special(SpecialKey::Escape), MetaName::Super).label, "⎋");
    EXPECT_EQ(keyLabel(special(SpecialKey::Left), MetaName::Super).label, "←");

    KeyPress tab = special(SpecialKey::Tab);
    tab.alt = true;
    EXPECT_EQ(keyLabel(tab, MetaName::Win).label, "Alt+⇥");

    KeyPress enter = special(SpecialKey::Return);
    enter.shift = true;
    EXPECT_EQ(keyLabel(enter, MetaName::Super).label, "Shift+↩");

    KeyPress f5 = special(SpecialKey::Function);
    f5.functionKey = 5;
    EXPECT_EQ(keyLabel(f5, MetaName::Super).label, "F5");
}

TEST(KeyLabel, ModifierAloneShowsNothing)
{
    KeyPress shift; // a modifier key has no character of its own
    shift.shift = true;
    EXPECT_TRUE(keyLabel(shift, MetaName::Super).label.isEmpty());

    KeyPress ctrl;
    ctrl.ctrl = true;
    EXPECT_TRUE(keyLabel(ctrl, MetaName::Win).label.isEmpty());
}

TEST(KeyLabel, MetaLetterIsAShortcut)
{
    KeyPress k = letter("l", "l");
    k.meta = true;
    EXPECT_EQ(keyLabel(k, MetaName::Win).label, "Win+L");
}
