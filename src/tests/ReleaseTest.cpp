#include "core/Release.h"

#include <gtest/gtest.h>

using namespace qr;

namespace
{
// Trimmed from api.github.com/repos/Elyg/quickrecord/releases/latest.
const QByteArray kLatest = R"({
  "html_url": "https://github.com/Elyg/quickrecord/releases/tag/v0.0.13",
  "tag_name": "v0.0.13",
  "name": "QuickRecord v0.0.13",
  "assets": [
    {"name": "QuickRecord-0.0.13-macos-arm64.dmg", "size": 61000000,
     "browser_download_url": "https://github.com/Elyg/quickrecord/releases/download/v0.0.13/QuickRecord-0.0.13-macos-arm64.dmg"},
    {"name": "QuickRecord-0.0.13-windows-x64-setup.exe", "size": 64523674,
     "browser_download_url": "https://github.com/Elyg/quickrecord/releases/download/v0.0.13/QuickRecord-0.0.13-windows-x64-setup.exe"},
    {"name": "QuickRecord-0.0.13-windows-x64.zip", "size": 90000000,
     "browser_download_url": "https://github.com/Elyg/quickrecord/releases/download/v0.0.13/QuickRecord-0.0.13-windows-x64.zip"},
    {"name": "QuickRecord-0.0.13-x86_64.AppImage", "size": 120000000,
     "browser_download_url": "https://github.com/Elyg/quickrecord/releases/download/v0.0.13/QuickRecord-0.0.13-x86_64.AppImage"}
  ]
})";
} // namespace

TEST(Release, ParsesTheLatestRelease)
{
    const std::optional<Release> r = parseRelease(kLatest);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->version, "0.0.13");
    EXPECT_EQ(r->page.toString(), "https://github.com/Elyg/quickrecord/releases/tag/v0.0.13");
    ASSERT_EQ(r->assets.size(), 4);
    EXPECT_EQ(r->assets[1].size, 64523674);
}

TEST(Release, RejectsWhatIsntARelease)
{
    EXPECT_FALSE(parseRelease(""));
    EXPECT_FALSE(parseRelease("not json"));
    // What a rate-limited request gets.
    EXPECT_FALSE(parseRelease(R"({"message": "API rate limit exceeded for 1.2.3.4."})"));
    EXPECT_FALSE(parseRelease(R"({"tag_name": "nightly", "html_url": "https://github.com/x"})"));
    EXPECT_FALSE(parseRelease(R"({"tag_name": "v1.2.3"})")); // no page
}

TEST(Release, ComparesVersions)
{
    EXPECT_TRUE(isNewer("0.0.13", "0.0.12"));
    EXPECT_TRUE(isNewer("0.1.0", "0.0.99"));
    EXPECT_TRUE(isNewer("0.0.10", "0.0.9")); // numerically, not as text
    EXPECT_FALSE(isNewer("0.0.12", "0.0.12"));
    EXPECT_FALSE(isNewer("0.0.11", "0.0.12"));
    EXPECT_FALSE(isNewer("0.0.13", "dev"));
    EXPECT_FALSE(isNewer("dev", "0.0.12"));
    EXPECT_TRUE(isReleaseVersion("1.2.3"));
    EXPECT_FALSE(isReleaseVersion("dev"));
    EXPECT_FALSE(isReleaseVersion("1.2"));
}

TEST(Release, FindsThePackageThatCanUpdateItself)
{
    const Release r = *parseRelease(kLatest);
    const std::optional<ReleaseAsset> appImage = findAsset(r, Package::AppImage);
    ASSERT_TRUE(appImage);
    EXPECT_EQ(appImage->name, "QuickRecord-0.0.13-x86_64.AppImage");
    const std::optional<ReleaseAsset> setup = findAsset(r, Package::WindowsSetup);
    ASSERT_TRUE(setup);
    EXPECT_EQ(setup->name, "QuickRecord-0.0.13-windows-x64-setup.exe");
    const std::optional<ReleaseAsset> dmg = findAsset(r, Package::MacDmg);
    ASSERT_TRUE(dmg);
    EXPECT_EQ(dmg->name, "QuickRecord-0.0.13-macos-arm64.dmg");

    Release older = r; // releases before the installer only had the zip
    older.assets.removeIf([](const ReleaseAsset& a) { return a.name.endsWith("-setup.exe"); });
    EXPECT_FALSE(findAsset(older, Package::WindowsSetup));
}
