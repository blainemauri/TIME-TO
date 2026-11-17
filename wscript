#
# Build rules for Time To (Pebble SDK 4.44+).
#
import json
import os.path

top = '.'
out = 'build'

# Optional firmware APIs: enabled automatically once the installed SDK exports them.
OPTIONAL_APIS = {
    'vibes_enqueue_custom_pattern_with_amplitudes': 'TT_HAVE_VIBE_AMPLITUDES',
}


def options(ctx):
    ctx.load('pebble_sdk')


def _embed_config_page(ctx):
    """Copy src/config/config.html into a JS module so PebbleKit JS can also open it
    as a data: URL on phones that do not support bundled settings pages."""
    source = ctx.path.find_node('src/config/config.html')
    if source is None:
        return
    target = ctx.path.make_node('src/pkjs/config_html.js')
    text = ('// Generated from src/config/config.html by wscript. Do not edit.\n'
            'module.exports = ' + json.dumps(source.read()) + ';\n')
    if not os.path.exists(target.abspath()) or target.read() != text:
        target.write(text)


def configure(ctx):
    ctx.load('pebble_sdk')
    for platform in ctx.env.TARGET_PLATFORMS:
        env = ctx.all_envs[platform]
        header = os.path.join(str(env.PEBBLE_SDK_PLATFORM), 'include', 'pebble.h')
        try:
            with open(header) as f:
                text = f.read()
        except (IOError, OSError):
            text = ''
        for symbol, define in OPTIONAL_APIS.items():
            if symbol in text:
                env.append_value('DEFINES', [define])
    _embed_config_page(ctx)


def build(ctx):
    ctx.load('pebble_sdk')
    _embed_config_page(ctx)

    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
