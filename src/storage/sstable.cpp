#include "storage/sstable.h"
#include "storage/compressor.h"
#include <cstring>
#include <algorithm>
#include <vector>

namespace minitsdb {

// ============================================================
//  CRC-32 实现（标准多项式 0xEDB88320）
// ============================================================
static const uint32_t kCrc32Table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t Crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = kCrc32Table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// SSTable 文件格式（可变 Header）:
// [Magic: 8 bytes] "MINITSDB"
// [Version: 4 bytes] uint32
// [Tag name len: 2 bytes] uint16
// [Tag name: N bytes]
// [Block count: 4 bytes] uint32
// --- 之后是 Block 数据 ---
// --- 末尾 4 字节 CRC32 ---

// ============================================================
//  SSTableWriter
// ============================================================
SSTableWriter::SSTableWriter(const std::string& filepath)
    : filepath_(filepath) {}

SSTableWriter::~SSTableWriter() {
    if (opened_) Close();
}

bool SSTableWriter::Open() {
    if (!file_.Open(filepath_, os::FileMode::READ_WRITE)) return false;

    // 写入完整的 32 字节 Header 占位
    uint8_t header[32] = {0};
    // Magic
    std::memcpy(header, "MINITSDB", 8);
    // Version = 2 (CRC enabled)
    uint32_t ver = SSTABLE_VERSION_CRC;
    std::memcpy(header + 8, &ver, 4);
    // Tag name 空
    // Block count 占位（后续更新）
    // 写入
    file_.Write(header, 32);
    file_size_ = 32;
    opened_ = true;
    return true;
}

void SSTableWriter::AddBlock(const CompressedBlock& block) {
    if (!opened_) return;

    block_offsets_.push_back(static_cast<size_t>(file_.Tell()));

    // 写入块数据
    file_.Write(&block.range.start, sizeof(Timestamp));
    file_.Write(&block.range.end, sizeof(Timestamp));

    uint32_t ts_len = static_cast<uint32_t>(block.timestamps.size());
    uint32_t val_len = static_cast<uint32_t>(block.values.size());
    file_.Write(&ts_len, sizeof(ts_len));
    file_.Write(block.timestamps.data(), ts_len);
    file_.Write(&val_len, sizeof(val_len));
    file_.Write(block.values.data(), val_len);

    if (range_.start == INT64_MAX || block.range.start < range_.start)
        range_.start = block.range.start;
    if (block.range.end > range_.end)
        range_.end = block.range.end;

    block_count_++;
}

void SSTableWriter::Close() {
    if (!opened_) return;

    // 记录实际数据结束位置（写入 AddBlock 后的位置）
    data_end_ = static_cast<size_t>(file_.Tell());

    // 更新 Header 中的 Block count
    file_.Seek(12, SEEK_SET);  // Magic(8) + Version(4)
    // Tag name: uint16 tag_len + N bytes 数据
    uint16_t tag_len = 0;
    file_.Write(&tag_len, sizeof(tag_len));
    // Block count
    file_.Write(&block_count_, sizeof(block_count_));

    // 分块读取计算 CRC32（避免大数据量时 OOM）
    // 使用 sstable.h 中定义的 Crc32 函数，分块更新
    uint32_t crc = 0xFFFFFFFF;
    uint8_t chunk[64 * 1024];  // 64KB 分块
    file_.Seek(0, SEEK_SET);
    size_t remaining = data_end_;
    while (remaining > 0) {
        size_t to_read = std::min(remaining, sizeof(chunk));
        size_t bytes = 0;
        if (!file_.Read(chunk, to_read, &bytes) || bytes == 0) break;
        for (size_t i = 0; i < bytes; i++) {
            crc = kCrc32Table[(crc ^ chunk[i]) & 0xFF] ^ (crc >> 8);
        }
        remaining -= bytes;
    }
    crc ^= 0xFFFFFFFF;

    file_.Seek(0, SEEK_END);
    file_.Write(&crc, sizeof(crc));
    file_size_ = data_end_ + sizeof(crc);

    file_.Close();
    opened_ = false;
}

// ============================================================
//  SSTableReader
// ============================================================
SSTableReader::SSTableReader(const std::string& filepath)
    : filepath_(filepath) {}

SSTableReader::~SSTableReader() {
    Close();
}

bool SSTableReader::Open() {
    if (!file_.Open(filepath_, os::FileMode::READ)) return false;

    if (!ReadHeader()) return false;
    if (!ReadBlockIndex()) return false;

    opened_ = true;
    return true;
}

void SSTableReader::Close() {
    if (opened_) {
        file_.Close();
        opened_ = false;
    }
}

bool SSTableReader::ReadHeader() {
    char magic[9] = {0};
    file_.Read(magic, 8);
    if (std::strncmp(magic, "MINITSDB", 8) != 0) return false;

    uint32_t version;
    size_t bytes_read = 0;
    if (!file_.Read(&version, sizeof(version), &bytes_read) ||
        bytes_read != sizeof(version)) return false;
    if (version != SSTABLE_VERSION_CRC) return false;

    // 跳到偏移 12 读取 tag_len（uint16）和 block_count
    file_.Seek(12, SEEK_SET);
    uint16_t tag_len;
    if (!file_.Read(&tag_len, sizeof(tag_len), &bytes_read) ||
        bytes_read != sizeof(tag_len)) return false;
    if (tag_len > 0) {
        std::vector<char> buf(tag_len);
        if (!file_.Read(buf.data(), tag_len, &bytes_read) ||
            bytes_read != tag_len) return false;
        tag_name_.assign(buf.data(), tag_len);
    }

    uint32_t block_count;
    if (!file_.Read(&block_count, sizeof(block_count), &bytes_read) ||
        bytes_read != sizeof(block_count)) return false;
    block_count_ = block_count;

    // Block 数据从偏移 32 开始（固定 Header 大小）
    file_.Seek(32, SEEK_SET);
    return true;  // allow empty SSTable
}

bool SSTableReader::ReadBlockIndex() {
    // Block 数据从 header_size 开始
    size_t header_size = static_cast<size_t>(file_.Tell());

    for (uint32_t i = 0; i < block_count_; i++) {
        BlockIndex idx;
        idx.file_offset = header_size;

        size_t br = 0;
        if (!file_.Read(&idx.range.start, sizeof(Timestamp), &br) ||
            br != sizeof(Timestamp)) return false;
        if (!file_.Read(&idx.range.end, sizeof(Timestamp), &br) ||
            br != sizeof(Timestamp)) return false;

        uint32_t ts_len, val_len;
        if (!file_.Read(&ts_len, sizeof(ts_len), &br) ||
            br != sizeof(ts_len)) return false;
        idx.ts_comp_size = ts_len;
        file_.Seek(ts_len, SEEK_CUR);
        if (!file_.Read(&val_len, sizeof(val_len), &br) ||
            br != sizeof(val_len)) return false;
        idx.val_comp_size = val_len;
        file_.Seek(val_len, SEEK_CUR);

        blocks_.push_back(idx);
        header_size = static_cast<size_t>(file_.Tell());

        // 更新时间范围
        if (range_.start == INT64_MAX || idx.range.start < range_.start)
            range_.start = idx.range.start;
        if (idx.range.end > range_.end)
            range_.end = idx.range.end;
    }

    // 验证 CRC32
    {
        // 获取文件总大小
        int64_t file_size = file_.Size();
        if (file_size < 4) return false;  // 至少要有 CRC 的空间

        // 读取末尾 CRC
        uint32_t stored_crc = 0;
        file_.Seek(static_cast<int64_t>(file_size) - 4, SEEK_SET);
        size_t br = 0;
        if (!file_.Read(&stored_crc, sizeof(stored_crc), &br) ||
            br != sizeof(stored_crc)) return false;

        // 计算数据部分的 CRC（排除末尾 4 字节）
        size_t data_len = static_cast<size_t>(file_size) - 4;
        // 分块读取计算 CRC（避免大数据量 OOM）
        uint32_t computed_crc = 0xFFFFFFFF;
        uint8_t chunk[64 * 1024];
        file_.Seek(0, SEEK_SET);
        size_t remaining = data_len;
        while (remaining > 0) {
            size_t to_read = std::min(remaining, sizeof(chunk));
            size_t bytes = 0;
            if (!file_.Read(chunk, to_read, &bytes) || bytes == 0) break;
            for (size_t j = 0; j < bytes; j++) {
                computed_crc = kCrc32Table[(computed_crc ^ chunk[j]) & 0xFF] ^ (computed_crc >> 8);
            }
            remaining -= bytes;
        }
        computed_crc ^= 0xFFFFFFFF;

        if (computed_crc != stored_crc) return false;
    }

    return true;
}

std::vector<DataPoint> SSTableReader::ReadRange(const TimeRange& range) {
    if (!opened_) return {};

    BlockCompressor decompressor;
    std::vector<DataPoint> result;

    for (const auto& idx : blocks_) {
        if (!idx.range.Overlaps(range)) continue;

        file_.Seek(static_cast<int64_t>(idx.file_offset), SEEK_SET);

        // 跳过 block header 到压缩数据
        Timestamp tmp;
        size_t br = 0;
        file_.Read(&tmp, sizeof(Timestamp), &br);
        file_.Read(&tmp, sizeof(Timestamp), &br);

        uint32_t ts_len, val_len;
        file_.Read(&ts_len, sizeof(ts_len), &br);

        CompressedBlock block;
        block.timestamps.resize(ts_len);
        file_.Read(block.timestamps.data(), ts_len, &br);

        file_.Read(&val_len, sizeof(val_len), &br);
        block.values.resize(val_len);
        file_.Read(block.values.data(), val_len, &br);

        auto points = decompressor.Decompress(block);
        for (auto& p : points) {
            if (p.ts >= range.start && p.ts <= range.end) {
                result.push_back(std::move(p));
            }
        }
    }

    return result;
}

} // namespace minitsdb
