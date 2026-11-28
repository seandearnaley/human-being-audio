# Studio design and product handoff

The [studio mark](studio/mark.svg) appears in the native About screens, the website header and browser tab. It also supplies the installers’ welcome and header artwork. Rebuild the shared high-density PNG and the native installer images with `bash design/studio/render-installers.sh` using resvg and ImageMagick. All studio products share [About controls](../modules/studio-ui/StudioAbout.h) and core Visage UI tokens in [modules/visage-ui](../modules/visage-ui). Product-specific art and instrument controls live next to each product.

| Surface | Editable source | Inspect the result |
| --- | --- | --- |
| Studio identity and download names | [Studio catalogue](../studio.json) | Inspect the installed VST3 factory, About sheet, installer and website |
| Livecut Redux | [Visage editor](../products/livecut-redux/ui), [skin guide](../products/livecut-redux/docs/skin.md) | Open the editor, press its wordmark, use Close or Escape |
| Tract Form & other Visage products | [Visage interface](../products/tract-form/ui), [brand sources](../products/tract-form/brand) | Click the product wordmark to open About. Build the dev preset, then run the product's `*-ui-snapshot` tool or `brand/build.py --screenshots` |
| Product photography and audio | Product screenshots, offline renders in [factory/showcase](../factory/showcase) and [research](../research) | The website displays actual editor captures and short lossless examples from the engines |

## From a control concept to a native editor

Start with the interaction, its parameter range, default, units, and response to mouse and keyboard input. Build a visual prototype using the team's design tool. For a dimensional control, a Blender scene can own the geometry, camera, material and lighting. Keep authored studio assets in a product or shared design directory with the font and texture licences. Mark reusable objects or collections as Blender assets so other designers can link them from an asset library.

Record the Blender version, scene, camera, output scale and control positions with an export. Blender can render in a headless build, for example `blender -b knob.blend -o //export/knob-#### -F PNG -f 1`. Keep editable sources with the product and commit the runtime export the plugin actually consumes. A C++ contributor then builds the editor without installing Blender.

Review the neutral, minimum, maximum, focus and disabled states in the real editor. Check the smallest supported editor size and a high-density display. Use the product's existing parameter ID for automation and session recall. Promote a control to the shared UI module when both products can consume the same behavior. Livecut's procedural scissors and Tract Form's Visage panels are examples of product-specific art.

[Blender command-line rendering](https://docs.blender.org/manual/en/latest/advanced/command_line/render.html)
