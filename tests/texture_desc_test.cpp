#include "test_check.h"
#include "texture.h"

int main()
{
    arti::test::Checker checker{ "texture_desc_test" };

    const arti::rendering::TextureDesc desc;
    ARTI_CHECK(checker, desc.texels.empty());
    ARTI_CHECK(checker, desc.width == 0);
    ARTI_CHECK(checker, desc.height == 0);
    ARTI_CHECK(checker, desc.format == arti::rendering::TextureFormat::RGBA8Srgb);
    ARTI_CHECK(checker, desc.generate_mipmaps);
    ARTI_CHECK(checker, desc.debug_name == "Texture");

    const arti::rendering::TextureInfo info;
    ARTI_CHECK(checker, info.mip_levels == 0);
    ARTI_CHECK(checker, !info.built_in);

    return checker.summary();
}
