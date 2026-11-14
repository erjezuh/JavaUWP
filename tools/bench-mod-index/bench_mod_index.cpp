// bench_mod_index.cpp - coste del indice de mods frente al sondeo de JAR.
//
// Herramienta de medicion, NO se ejecuta en CI: crea un directorio temporal con
// JAR sinteticos y compara dos caminos:
//
//   1. "sondeo": abrir cada JAR como ZIP (leer el EOCD del final y el
//      directorio central), que es lo que hacia el launcher antes del indice.
//   2. "indice": una enumeracion del directorio + leer mod-index.tsv + resolver
//      los mismos mods en memoria.
//
// Los JAR son sinteticos, pero el trabajo de E/S del camino 1 es el mismo que
// hace miniz al abrir un JAR real (dos lecturas por fichero, una al final del
// archivo y otra en el directorio central). Uso:
//
//   g++ -std=c++17 -O2 -o /tmp/bench_mod_index \
//       tools/bench-mod-index/bench_mod_index.cpp -IMC.Xbox/mods
//   /tmp/bench_mod_index [numJars] [rondas]

#include "mod_index.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct JarInfo {
    fs::path path;
    std::string stem;
    unsigned long long size = 0;
    unsigned long long writeTime = 0;
};

unsigned long long WriteSyntheticJar(const fs::path& path, int index, std::size_t payloadBytes) {
    // Cuerpo relleno (como un JAR lleno de clases) + un EOCD valido al final,
    // de modo que el camino "sondeo" localice el directorio central.
    std::vector<char> payload(payloadBytes, static_cast<char>('A' + (index % 26)));
    for (std::size_t i = 0; i < payload.size(); i += 97) {
        payload[i] = static_cast<char>((index * 31 + static_cast<int>(i)) & 0x7F);
    }
    std::vector<unsigned char> eocd(22, 0);
    eocd[0] = 0x50;
    eocd[1] = 0x4B;
    eocd[2] = 0x05;
    eocd[3] = 0x06;
    // entradas totales en el directorio central (offset 10) = 4
    eocd[10] = 4;
    // tamano del directorio central (offset 12) = 512
    eocd[12] = 0x00;
    eocd[13] = 0x02;
    // offset del directorio central (offset 16) = tamano del cuerpo
    const std::uint32_t bodySize = static_cast<std::uint32_t>(payload.size());
    std::memcpy(&eocd[16], &bodySize, sizeof(bodySize));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    // 512 bytes de "directorio central" para que la segunda lectura tenga cuerpo.
    std::vector<char> central(512, 0);
    central[0] = 'P';
    central[1] = 'K';
    central[2] = 0x01;
    central[3] = 0x02;
    out.write(central.data(), static_cast<std::streamsize>(central.size()));
    out.write(reinterpret_cast<const char*>(eocd.data()), static_cast<std::streamsize>(eocd.size()));
    out.close();
    return static_cast<unsigned long long>(payload.size() + central.size() + eocd.size());
}

// Abre el JAR como ZIP: lee el EOCD del final y despues el directorio central.
// Devuelve los bytes leidos, para poder compararlos con el camino indexado.
unsigned long long ProbeJar(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    in.seekg(0, std::ios::end);
    const std::streamoff total = in.tellg();
    if (total < 22) return 0;
    std::vector<char> tail(22, 0);
    in.seekg(total - 22);
    in.read(tail.data(), 22);
    in.clear();
    unsigned long long left = static_cast<unsigned long long>(in.gcount());
    std::uint32_t centralOffset = 0;
    std::uint32_t centralSize = 0;
    std::memcpy(&centralSize, tail.data() + 12, sizeof(centralSize));
    std::memcpy(&centralOffset, tail.data() + 16, sizeof(centralOffset));
    if (centralSize > 0 && centralOffset + centralSize <= static_cast<std::uint32_t>(total)) {
        std::vector<char> central(centralSize, 0);
        in.seekg(centralOffset);
        in.read(central.data(), static_cast<std::streamsize>(centralSize));
        left += static_cast<unsigned long long>(in.gcount());
    }
    return left;
}

double MeanMs(const std::vector<double>& samples) {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (double value : samples) sum += value;
    return sum / static_cast<double>(samples.size());
}

}  // namespace

int main(int argc, char** argv) {
    const int jars = argc > 1 ? std::max(1, std::atoi(argv[1])) : 150;
    const int rounds = argc > 2 ? std::max(1, std::atoi(argv[2])) : 20;
    const std::size_t payloadBytes = 350 * 1024;

    const fs::path dir = fs::temp_directory_path() / "bandit-bench-mods";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    std::vector<JarInfo> infos;
    infos.reserve(static_cast<std::size_t>(jars));
    unsigned long long totalBytes = 0;
    for (int i = 0; i < jars; ++i) {
        char stem[32];
        std::snprintf(stem, sizeof(stem), "mod-%03d-1.2.%d", i, i % 7);
        char file[64];
        std::snprintf(file, sizeof(file), "%s.jar", stem);
        JarInfo info;
        info.path = dir / file;
        info.stem = stem;
        info.size = WriteSyntheticJar(info.path, i, payloadBytes);
        info.writeTime = static_cast<unsigned long long>(1000000 + i * 977);
        totalBytes += info.size;
        infos.push_back(info);
    }

    // El indice que escribiria el launcher la primera vez que ve el pack.
    modindex::Entries entries;
    entries.reserve(infos.size());
    for (const JarInfo& info : infos) {
        modindex::Entry entry;
        entry.name = info.path.filename().string();
        entry.size = info.size;
        entry.writeTime = info.writeTime;
        entry.fabricId = info.stem;
        entry.probed = true;
        entries.push_back(entry);
    }
    modindex::SortEntries(entries);
    const std::string serialized = modindex::Serialize(entries);
    const fs::path indexPath = dir / "mod-index.tsv";
    {
        std::ofstream out(indexPath, std::ios::binary | std::ios::trunc);
        out.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    }

    std::vector<double> probeSamples;
    std::vector<double> indexSamples;
    std::vector<double> tableSamples;
    unsigned long long probeBytes = 0;
    unsigned long long indexBytes = 0;
    int resolved = 0;

    for (int round = 0; round < rounds; ++round) {
        auto start = std::chrono::steady_clock::now();
        for (const JarInfo& info : infos) probeBytes += ProbeJar(info.path);
        auto mid = std::chrono::steady_clock::now();
        probeSamples.push_back(std::chrono::duration<double, std::milli>(mid - start).count());

        start = std::chrono::steady_clock::now();
        modindex::Entries cached;
        {
            std::ifstream in(indexPath, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            indexBytes += text.size();
            if (!modindex::Parse(text, cached)) {
                std::fprintf(stderr, "bench: index parse failed\n");
                return 1;
            }
        }
        resolved = 0;
        for (const JarInfo& info : infos) {
            const modindex::Lookup found = modindex::LookupMod(cached, info.stem, info.stem);
            if (found.index >= 0) ++resolved;
        }
        mid = std::chrono::steady_clock::now();
        indexSamples.push_back(std::chrono::duration<double, std::milli>(mid - start).count());

        // Camino de produccion: indice en memoria + tabla derivada + resolucion.
        start = std::chrono::steady_clock::now();
        const modindex::LookupTable table = modindex::BuildLookupTable(cached);
        int resolvedByTable = 0;
        for (const JarInfo& info : infos) {
            const modindex::Lookup found = modindex::LookupMod(cached, table, info.stem, info.stem);
            if (found.index >= 0) ++resolvedByTable;
        }
        mid = std::chrono::steady_clock::now();
        tableSamples.push_back(std::chrono::duration<double, std::milli>(mid - start).count());
        if (resolvedByTable != resolved) {
            std::fprintf(stderr, "bench: the table path resolved %d, the slow path %d\n",
                         resolvedByTable, resolved);
            return 1;
        }
    }

    const double probeMs = MeanMs(probeSamples);
    const double indexMs = MeanMs(indexSamples);
    const double tableMs = MeanMs(tableSamples);
    std::printf("jars=%d rounds=%d payload=%zuKiB pack=%.1fMiB index=%zuB resolved=%d/%d\n",
                jars, rounds, payloadBytes / 1024, static_cast<double>(totalBytes) / (1024.0 * 1024.0),
                serialized.size(), resolved, jars);
    std::printf("probe every jar (open ZIP + central directory): %8.3f ms/round  %llu B read\n",
                probeMs, probeBytes / static_cast<unsigned long long>(rounds));
    std::printf("mod index (read TSV + resolve in memory):        %8.3f ms/round  %llu B read\n",
                indexMs, indexBytes / static_cast<unsigned long long>(rounds));
    std::printf("mod index + lookup table (production path):      %8.3f ms/round\n", tableMs);
    if (indexMs > 0.0) {
        std::printf("table vs slow lookup: %.1fx faster in memory\n", indexMs / std::max(1e-9, tableMs));
        std::printf("speed-up: %.1fx  (bytes read: %.0fx less)\n", probeMs / tableMs,
                    static_cast<double>(probeBytes) / std::max<unsigned long long>(1, indexBytes));
        std::printf("probe vs production path: %.0fx fewer bytes, %.1fx less wall clock\n",
                    static_cast<double>(probeBytes) / std::max<unsigned long long>(1, indexBytes),
                    probeMs / std::max(1e-9, tableMs));
    }

    fs::remove_all(dir, ec);
    return 0;
}
