import { mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'

const projectRoot = resolve(import.meta.dir, '..')
const cacheDir = resolve(projectRoot, '.font-cache')
const fontPath = resolve(cacheDir, 'NotoSansCJKsc-Regular.otf')
const fontUrl = 'https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf'

async function gb2312Hanzi() {
  const encoded = new Uint8Array(72 * 94 * 2)
  let offset = 0
  for (let lead = 0xb0; lead <= 0xf7; lead += 1) {
    for (let trail = 0xa1; trail <= 0xfe; trail += 1) {
      encoded[offset] = lead
      encoded[offset + 1] = trail
      offset += 2
    }
  }
  const iconv = Bun.spawn(['iconv', '-c', '-f', 'GB2312', '-t', 'UTF-8'], {
    stdin: encoded,
    stdout: 'pipe',
    stderr: 'inherit',
  })
  const decoded = await new Response(iconv.stdout).text()
  if (await iconv.exited !== 0) throw new Error('iconv failed to decode GB2312')
  const characters = new Set<string>()
  for (const character of decoded) {
    if (character !== '\ufffd' && /^\p{Script=Han}$/u.test(character)) {
      characters.add(character)
    }
  }
  if (characters.size !== 6763) {
    throw new Error(`Expected 6763 GB2312 Hanzi, got ${characters.size}`)
  }
  return [...characters].join('')
}

async function ensureFont() {
  const cached = Bun.file(fontPath)
  if (await cached.exists()) return
  await mkdir(cacheDir, { recursive: true })
  const download = Bun.spawn(['curl', '-fL', '--retry', '3', '-o', fontPath, fontUrl], {
    stdout: 'inherit',
    stderr: 'inherit',
  })
  if (await download.exited !== 0) throw new Error('Font download failed')
}

async function generate(size: 14 | 16, symbols: string) {
  const output = `src/ui_font_zh_${size}.c`
  const codepoints = [...symbols].map((character) => character.codePointAt(0)!).sort((a, b) => a - b)
  const ranges: string[] = []
  let start = codepoints[0]!
  let end = start
  for (const codepoint of codepoints.slice(1)) {
    if (codepoint === end + 1) {
      end = codepoint
      continue
    }
    ranges.push(start === end ? `0x${start.toString(16)}` : `0x${start.toString(16)}-0x${end.toString(16)}`)
    start = codepoint
    end = codepoint
  }
  ranges.push(start === end ? `0x${start.toString(16)}` : `0x${start.toString(16)}-0x${end.toString(16)}`)
  const process = Bun.spawn([
    'bunx', '--bun', 'lv_font_conv@1.5.3',
    '--size', String(size),
    '--bpp', '1',
    '--format', 'lvgl',
    '--font', '.font-cache/NotoSansCJKsc-Regular.otf',
    '--range', ranges.join(','),
    '--no-kerning',
    '--lv-fallback', `lv_font_montserrat_${size}`,
    '--lv-include', 'lvgl.h',
    '--lv-font-name', `ui_font_zh_${size}`,
    '-o', output,
  ], { cwd: projectRoot, stdout: 'inherit', stderr: 'inherit' })
  const exitCode = await process.exited
  if (exitCode !== 0) throw new Error(`lv_font_conv failed for ${size}px`)
  const generated = await Bun.file(resolve(projectRoot, output)).text()
  await Bun.write(resolve(projectRoot, output), `${generated.trimEnd()}\n`)
}

const printableAscii = Array.from({ length: 0x7f - 0x20 }, (_, index) =>
  String.fromCharCode(0x20 + index),
).join('')
const symbols = printableAscii + await gb2312Hanzi()
await ensureFont()
await generate(14, symbols)
await generate(16, symbols)
console.log(`Generated 14px and 16px LVGL fonts with printable ASCII and 6763 GB2312 Hanzi.`)
