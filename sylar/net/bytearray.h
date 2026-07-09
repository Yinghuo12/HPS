#ifndef __SYLAR_BYTEARRAY_H__
#define __SYLAR_BYTEARRAY_H__

#include <memory>
#include <string>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <vector>

namespace sylar {

class ByteArray {
public:
    typedef std::shared_ptr<ByteArray> ptr;

    struct Node {
        Node();
        Node(size_t s);
        ~Node();

        char* ptr;
        Node* next;
        size_t size;
    };

    ByteArray(size_t base_size = 4096);
    ~ByteArray();

    /* write */
    // 固定长度
    void writeFint8  (int8_t value);
    void writeFuint8 (uint8_t value);
    void writeFint16 (int16_t value);
    void writeFuint16(uint16_t value);
    void writeFint32 (int32_t value);
    void writeFuint32(uint32_t value);
    void writeFint64 (int64_t value);
    void writeFuint64(uint64_t value);

    // 可变长度
    void writeInt32  (int32_t value);
    void writeUint32 (uint32_t value);
    void writeInt64  (int64_t value);
    void writeUint64 (uint64_t value);

    void writeFloat  (float value);
    void writeDouble (double value);
    //length:int16 , data
    void writeStringF16(const std::string& value);
    //length:int32 , data
    void writeStringF32(const std::string& value);
    //length:int64 , data
    void writeStringF64(const std::string& value);
    //length:varint, data
    void writeStringVint(const std::string& value);
    //data
    void writeStringWithoutLength(const std::string& value);

    /* read */
    // 固定长度
    int8_t   readFint8();
    uint8_t  readFuint8();
    int16_t  readFint16();
    uint16_t readFuint16();
    int32_t  readFint32();
    uint32_t readFuint32();
    int64_t  readFint64();
    uint64_t readFuint64();

    // 可变长度
    int32_t  readInt32();
    uint32_t readUint32();
    int64_t  readInt64();
    uint64_t readUint64();

    float    readFloat();
    double   readDouble();

    //length:int16, data
    std::string readStringF16();
    //length:int32, data
    std::string readStringF32();
    //length:int64, data
    std::string readStringF64();
    //length:varint , data
    std::string readStringVint();

    /* 内部操作 */
    void clear();

    // 真正的读写操作
    void write(const void* buf, size_t size);
    void read(void* buf, size_t size);
    void read(void* buf, size_t size, size_t position) const;

    size_t getPosition() const { return m_position;}
    void setPosition(size_t v);

    bool writeToFile(const std::string& name) const;
    bool readFromFile(const std::string& name);

    size_t getBaseSize() const { return m_baseSize;}
    size_t getReadSize() const { return m_size - m_position;}

    bool isLittleEndian() const;
    void setIsLittleEndian(bool val);

    std::string toString() const;
    std::string toHexString() const;

    //只获取内容，不修改position
    uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len = ~0ull) const;             // 基于当前读指针 m_position
    uint64_t getReadBuffers(std::vector<iovec>& buffers, uint64_t len, uint64_t position) const;  // 基于指定位置 position
    //增加容量，不修改position 
    uint64_t getWriteBuffers(std::vector<iovec>& buffers, uint64_t len);

    size_t getSize() const { return m_size;}
private:
    void addCapacity(size_t size);
    size_t getCapacity() const { return m_capacity - m_position;}
private:
    size_t m_baseSize;    // 初始容量
    size_t m_position;    // 当前读写的位置
    size_t m_capacity;    // 当前容量
    size_t m_size;        // 当前数据大小
    int8_t m_endian;      // 0:little, 1:big
    Node* m_root;         // 第一个内存块
    Node* m_cur;          // 当前操作的内存块
};

}

#endif