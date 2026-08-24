#ifndef TINK_H
#define TINK_H

#include <QFile>
#include <QObject>
#include <QSerialPort>
#include <QTimer>

enum class GroupStatus {
    NotApplicable,   // not in Linux
    NoSuchGroup,     // group doesn't exist
    NotAMember,
    ConfiguredOnly,  // in the DB, but not in this session (reboot required)
    Active
};

// Fork of the tinkup.py RetroTINK bootloader engine.
//
// Frames are SOH <payload> <crc16-lo> <crc16-hi> EOT, with SOH/EOT/DLE
// bytes inside the frame escaped by DLE. The bootloader is driven through
// the CmdGetVer -> CmdErase -> CmdWrite... -> JumpApp sequence, streaming
// one Firmware HEX record per CmdWrite.
class Tink : public QObject
{
    Q_OBJECT

public:
    static constexpr char CmdGetVer = '\x01';
    static constexpr char CmdErase  = '\x02';
    static constexpr char CmdWrite  = '\x03';
    static constexpr char JumpApp   = '\x05';

    static constexpr char SOH = '\x01';
    static constexpr char EOT = '\x04';
    static constexpr char DLE = '\x10';

    enum RxState { RxIdle, RxBuffer, RxEscape };
    enum BlState { BlIdle, BlVersion, BlErase, BlWrite, BlJump };

    // these functions helps determining if a user is a member of dialup
    static GroupStatus checkGroup(const char *groupName);
    static QString currentUserName();

    // Probe/write acks arrive quickly, a full flash erase doesn't.
    static constexpr int ResponseTimeoutMs = 5000;
    static constexpr int EraseTimeoutMs = 60000;

    // Progress is reported when transmission crosses these steps
    static constexpr int CheckpointStep = 5;

    explicit Tink(QObject *parent = nullptr);
    ~Tink() override;

    bool isRunning() const { return m_blState != BlIdle; }

    // CRC-16/XMODEM (polynomial 0x1021, init 0)
    static quint16 calcCrc(const QByteArray &data);

    // Wrap a payload into a full frame: append CRC, escape control
    // bytes with DLE, delimit with SOH/EOT.
    static QByteArray buildFrame(const QByteArray &payload);

    // Close any open buffers and the serial port if it is open.
    void cleanup();

public slots:
    void start(const QString &fwName, const QString &portName);

signals:
    // Emitted once per update, when the first firmware frame goes out
    void writingStarted();
    // Emitted only when the transmitted frame count crosses a
    // CheckpointStep checkpoint (5, 10, 15, ... 100)
    void progress(int percent);
    void finished(bool success);

private slots:
    void onReadyRead();
    void onResponseTimeout();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    friend class TestTink; // unit tests drive the private state machines

    bool validateHexFile();
    void txPacket(const QByteArray &payload);
    void rxByte(char b);
    void rxProcess(const QByteArray &packet);
    bool sendNextWrite();
    void fail(const QString &reason);

    QSerialPort m_serial;
    QTimer m_responseTimer;
    QFile m_fwFile;
    QString m_fwName;

    RxState m_rxState = RxIdle;
    BlState m_blState = BlIdle;
    QByteArray m_rxBuf;

    int m_hexLine = 0;
    int m_hexNline = 0;
    int m_nextCheckpoint = CheckpointStep;
};

#endif // TINK_H
