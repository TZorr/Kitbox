//
//  TransmuteGolden/main.swift
//  Kitbox
//
//  Writes reference output from Transmute's own Swift engine, for KitboxCheck
//  to hold the C++ port against. Compiled together with Transmute/Engine by
//  Verification/transmute_golden.sh; not part of any build product.
//
//  For every audio file under the given folder (Transmute's Test Samples):
//    <name>.in.f32           the hit as Transmute decodes it: mono, 48 kHz
//    <name>.source.txt       the file's own sample rate
//    <name>.file.txt         the file's path
//    <name>.initial.drumparams / .suggested.txt / .floor.txt
//                            the analysis: first guess, suggested model, noise floor
//    <name>.fit.drumparams   the fit, and <name>.score.txt its Match (total dB)
//    <name>.render48.f32     the fit rendered at 48 kHz
//    <name>.render44.f32     at 44.1 kHz, with filter, master envelope and a set
//                            Length on top, so those paths are covered too
//
//  Usage: transmute_golden <samples folder> <output folder>
//

import Foundation

func writeFloats(_ values: [Float], to url: URL) throws {
    try values.withUnsafeBufferPointer { Data(buffer: $0) }.write(to: url)
}

func writeText(_ text: String, to url: URL) throws {
    try text.write(to: url, atomically: true, encoding: .utf8)
}

let arguments = CommandLine.arguments
guard arguments.count == 3 else {
    print("usage: transmute_golden <samples folder> <output folder>")
    exit(2)
}
let input = URL(fileURLWithPath: arguments[1])
let output = URL(fileURLWithPath: arguments[2])
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

let files = BatchConvert.audioFiles(in: [input])
print("\(files.count) files")

for url in files {
    let folder = url.deletingLastPathComponent().lastPathComponent
    let name = "\(folder) - \(url.deletingPathExtension().lastPathComponent)"
        .trimmingCharacters(in: .whitespaces)
    let base = output.appendingPathComponent(name)
    func file(_ suffix: String) -> URL { URL(fileURLWithPath: base.path + suffix) }

    let started = Date()
    let (audio, info) = try Decoder.decode(url)
    try writeFloats(audio.array, to: file(".in.f32"))
    try writeText(String(info.sampleRate), to: file(".source.txt"))
    try writeText(url.path, to: file(".file.txt"))

    let analysis = try Analyzer.analyze(audio)
    try analysis.initial.write(to: file(".initial.drumparams"))
    try writeText(analysis.suggested.rawValue, to: file(".suggested.txt"))
    try writeText(String(analysis.noiseFloorDB), to: file(".floor.txt"))

    let report = try Fitter.fit(analysis)
    try report.params.write(to: file(".fit.drumparams"))
    try writeText(String(report.score.total), to: file(".score.txt"))
    try writeFloats(DrumSynth.render(report.params, sampleRate: 48_000), to: file(".render48.f32"))

    var shaped = report.params
    shaped.filterType = .lowPass
    shaped.filterCutoff = 3000
    shaped.filterQ = 2
    shaped.envelopeOn = true
    shaped.envHold = 0.05
    shaped.envRelease = 0.2
    shaped.autoLength = false
    shaped.length = 0.3
    try shaped.write(to: file(".shaped.drumparams"))
    try writeFloats(DrumSynth.render(shaped, sampleRate: 44_100), to: file(".render44.f32"))

    print(String(format: "%@  %@  %.3f dB  %.1f s", name, analysis.suggested.rawValue,
                 report.score.total, Date().timeIntervalSince(started)))
}
