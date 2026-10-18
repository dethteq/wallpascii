// Generates the wallpascii app icon: shade blocks ░▒▓█ running diagonally
// across a flat gray macOS squircle, each pixel a flat fire color.
//
//   swift assets/icon/make_icon.swift assets/icon
//
// Writes icon.svg (master), icon-1024.png, AppIcon.iconset/ and AppIcon.icns.
// Geometry follows the macOS icon grid: 1024 canvas, 824 body inset by 100.

import AppKit
import Foundation

let canvas = 1024.0
let body = CGRect(x: 100, y: 100, width: 824, height: 824)
let env = ProcessInfo.processInfo.environment
let cells = Int(env["CELLS"] ?? "") ?? 6  // cells x cells character cells
let sub = Int(env["SUB"] ?? "") ?? 2      // each cell is a sub x sub dot grid for the shade patterns
// Washed-out warm blacks; every background pixel takes one of them, like tape grain.
let grays = [(0x17, 0x15, 0x17), (0x1C, 0x1A, 0x1C), (0x22, 0x1F, 0x21), (0x29, 0x26, 0x27)]
let background = grays[0]
// Faded pastel fire from cold to hot; pixels step through them along the diagonal.
let fire = [(0x7A, 0x2B, 0x2B), (0xC2, 0x4A, 0x3E), (0xE0, 0x7B, 0x4A), (0xEB, 0xA8, 0x5C), (0xF0, 0xD2, 0x7E)]
// Every pixel's brightness wobbles by up to this much, like analog noise.
let grain = 0.05

// macOS continuous-corner rectangle: straight edges, each corner a quarter
// superellipse spanning `corner` px, which matches Apple's 185px-radius squircle.
func squirclePoints() -> [CGPoint] {
    let n = 4.0, corner = 300.0, steps = 90
    // Corner centers and the direction the corner bulges toward (y down).
    let corners: [(CGPoint, Double, Double)] = [
        (CGPoint(x: body.maxX - corner, y: body.minY + corner), 1, -1),  // top right
        (CGPoint(x: body.maxX - corner, y: body.maxY - corner), 1, 1),   // bottom right
        (CGPoint(x: body.minX + corner, y: body.maxY - corner), -1, 1),  // bottom left
        (CGPoint(x: body.minX + corner, y: body.minY + corner), -1, -1), // top left
    ]
    var pts: [CGPoint] = []
    for (i, (c, sx, sy)) in corners.enumerated() {
        for k in 0...steps {
            // Walk clockwise: each corner sweeps from one edge to the next.
            var t = Double(k) / Double(steps) * .pi / 2
            if i % 2 == 0 { t = .pi / 2 - t }
            let x = pow(cos(t), 2 / n), y = pow(sin(t), 2 / n)
            pts.append(CGPoint(x: c.x + sx * corner * x, y: c.y + sy * corner * y))
        }
    }
    return pts
}

// Shade level 0 (space) ... 4 (█) per cell: empty top-left, solid bottom-right.
func level(_ col: Int, _ row: Int) -> Int {
    let t = Double(col + row) / Double(2 * (cells - 1))
    return min(4, Int(t * 5))
}

func dotOn(_ lvl: Int, _ x: Int, _ y: Int) -> Bool {
    switch lvl {
    case 1: return x % 2 == 0 && y % 2 == 0      // ░
    case 2: return (x + y) % 2 == 0              // ▒
    case 3: return !(x % 2 == 1 && y % 2 == 1)   // ▓
    case 4: return true                          // █
    default: return false
    }
}

// Fire color of one dot: bands along the diagonal, with a fixed per-pixel
// jitter so neighbouring bands interleave like flickering pixel fire.
func pixelHash(_ x: Int, _ y: Int, _ salt: Int = 0) -> UInt32 {
    var h = UInt32(truncatingIfNeeded: x &* 73856093 ^ y &* 19349663 ^ salt &* 83492791)
    h = (h ^ (h >> 13)) &* 0x5bd1e995
    return h ^ (h >> 15)
}

func fireIndex(_ x: Int, _ y: Int, _ n: Int) -> Int {
    let jitter = Double(pixelHash(x, y) % 1000) / 1000 - 0.5
    let t = Double(x + y) / Double(2 * (n - 1))
    return max(0, min(fire.count - 1, Int((t * 4.6 - 0.4 + jitter * 0.9).rounded(.down))))
}

// Every pixel of the body as a flat color: fire tones for lit dots, grain
// grays elsewhere, each nudged by `grain`. Pattern dots the rounded corners
// would cut are dropped; solid fill is clipped by the squircle.
func pixelRects() -> [(color: (Int, Int, Int), rects: [CGRect])] {
    let n = cells * sub
    let d = body.width / Double(n)
    let shape = squirclePath()
    func inside(_ x: Int, _ y: Int) -> Bool {
        let r = CGRect(x: body.minX + Double(x) * d, y: body.minY + Double(y) * d, width: d, height: d)
            .insetBy(dx: 0.5, dy: 0.5)
        return [CGPoint(x: r.minX, y: r.minY), CGPoint(x: r.maxX, y: r.minY),
                CGPoint(x: r.minX, y: r.maxY), CGPoint(x: r.maxX, y: r.maxY)].allSatisfy { shape.contains($0) }
    }
    var groups: [String: ((Int, Int, Int), [CGRect])] = [:]
    for y in 0..<n {
        for x in 0..<n {
            let lvl = level(x / sub, y / sub)
            let lit = dotOn(lvl, x % sub, y % sub) && (lvl == 4 || inside(x, y))
            let base = lit ? fire[fireIndex(x, y, n)] : grays[Int(pixelHash(x, y, 1) % UInt32(grays.count))]
            let k = 1 + grain * (Double(pixelHash(x, y, 2) % 1000) / 500 - 1)
            func ch(_ v: Int) -> Int { max(0, min(255, Int((Double(v) * k).rounded()))) }
            let c = (ch(base.0), ch(base.1), ch(base.2))
            let rect = CGRect(x: body.minX + Double(x) * d, y: body.minY + Double(y) * d, width: d, height: d)
            groups[hex(c), default: (c, [])].1.append(rect)
        }
    }
    return groups.keys.sorted().map { groups[$0]! }
}

func hex(_ c: (Int, Int, Int)) -> String { String(format: "#%02X%02X%02X", c.0, c.1, c.2) }
func cg(_ c: (Int, Int, Int), _ a: Double = 1) -> CGColor {
    CGColor(srgbRed: Double(c.0) / 255, green: Double(c.1) / 255, blue: Double(c.2) / 255, alpha: a)
}

func squirclePath() -> CGPath {
    let p = CGMutablePath()
    p.addLines(between: squirclePoints())
    p.closeSubpath()
    return p
}

func render(size: Int) -> Data {
    let cs = CGColorSpace(name: CGColorSpace.sRGB)!
    let ctx = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                        space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    let s = Double(size) / canvas
    // Flip to y-down so the geometry matches the SVG.
    ctx.translateBy(x: 0, y: Double(size))
    ctx.scaleBy(x: s, y: -s)

    let shape = squirclePath()

    // Drop shadow (macOS template: soft, slightly below).
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -12 * s), blur: 28 * s, color: cg((0, 0, 0), 0.35))
    ctx.addPath(shape)
    ctx.setFillColor(cg(background))
    ctx.fillPath()
    ctx.restoreGState()

    ctx.addPath(shape)
    ctx.clip()
    ctx.setShouldAntialias(false)  // crisp pixel edges
    for (color, rects) in pixelRects() {
        ctx.setFillColor(cg(color))
        ctx.fill(rects)
    }

    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    return rep.representation(using: .png, properties: [:])!
}

func svg() -> String {
    let pts = squirclePoints().map { String(format: "%.2f,%.2f", $0.x, $0.y) }.joined(separator: " ")
    let pixels = pixelRects().map { color, rects in
        let d = rects.map { r in
            String(format: "M%.2f %.2fh%.2fv%.2fh%.2fz", r.minX, r.minY, r.width, r.height, -r.width)
        }.joined()
        return "  <path d=\"\(d)\" fill=\"\(hex(color))\" clip-path=\"url(#body)\" shape-rendering=\"crispEdges\"/>"
    }.joined(separator: "\n")
    return """
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
      <defs>
        <clipPath id="body"><polygon points="\(pts)"/></clipPath>
        <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
          <feDropShadow dx="0" dy="12" stdDeviation="14" flood-color="#000" flood-opacity="0.35"/>
        </filter>
      </defs>
      <polygon points="\(pts)" fill="\(hex(background))" filter="url(#shadow)"/>
    \(pixels)
    </svg>

    """
}

let out = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ".")
let iconset = out.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)

try svg().write(to: out.appendingPathComponent("icon.svg"), atomically: true, encoding: .utf8)
try render(size: 1024).write(to: out.appendingPathComponent("icon-1024.png"))
for base in [16, 32, 128, 256, 512] {
    try render(size: base).write(to: iconset.appendingPathComponent("icon_\(base)x\(base).png"))
    try render(size: base * 2).write(to: iconset.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}

let p = Process()
p.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
p.arguments = ["-c", "icns", iconset.path, "-o", out.appendingPathComponent("AppIcon.icns").path]
try p.run()
p.waitUntilExit()
print(p.terminationStatus == 0 ? "ok: \(out.path)" : "iconutil failed")
