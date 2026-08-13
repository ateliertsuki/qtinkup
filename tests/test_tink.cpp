#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>

#include "tink.h"

// Expected CRC values were generated with the calc_crc() function of the
// original tinkup.py, so these tests verify against the reference
// implementation (the algorithm is CRC-16/XMODEM: poly 0x1021, init 0).
class TestTink : public QObject
{
    Q_OBJECT

private slots:
    // --- CRC ---
    void crcVectors_data();
    void crcVectors();

    // --- TX framing ---
    void frameAppendsCrcAndDelimiters();
    void frameEscapesControlBytes();
    void frameEscapesControlBytesInCrc();

    // --- RX deframing ---
    void rxAssemblesPacket();
    void rxUnescapesControlBytes();
    void rxIgnoresNoiseBeforeSoh();
    void rxRejectsBadCrc();

    // --- Firmware file validation ---
    void hexFileValid();
    void hexFileBadChecksum();
    void hexFileMissing();

    // --- Bootloader session ---
    void fullUpdateSession();

private:
    // Feed raw wire bytes into the receive state machine
    static void feed(Tink &tink, const QByteArray &bytes)
    {
        for (char b : bytes)
            tink.rxByte(b);
    }

    // Write an Intel HEX file (valid checksums) and return its path
    static QString makeHexFile(QTemporaryFile &file)
    {
        // Two records of a real-world Intel HEX layout
        file.open();
        file.write(":020000040000FA\n");
        file.write(":00000001FF\n");
        file.close();
        return file.fileName();
    }
};

void TestTink::crcVectors_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<int>("expected");

    QTest::newRow("empty")      << QByteArray()                        << 0x0000;
    QTest::newRow("CmdGetVer")  << QByteArray("\x01", 1)               << 0x1021;
    QTest::newRow("CmdErase")   << QByteArray("\x02", 1)               << 0x2042;
    QTest::newRow("JumpApp")    << QByteArray("\x05", 1)               << 0x50A5;
    QTest::newRow("xmodem-check") << QByteArray("123456789")           << 0x31C3;
    QTest::newRow("ctrl-bytes") << QByteArray("\x03\x10\x01\x04", 4)   << 0xAB0A;
    QTest::newRow("all-ff")     << QByteArray(4, char(0xFF))           << 0x99CF;
    QTest::newRow("hex-record")
        << QByteArray("\x03", 1) + QByteArray::fromHex("020000040000fa")
        << 0x2C32;
}

void TestTink::crcVectors()
{
    QFETCH(QByteArray, input);
    QFETCH(int, expected);
    QCOMPARE(int(Tink::calcCrc(input)), expected);
}

void TestTink::frameAppendsCrcAndDelimiters()
{
    // CmdGetVer (0x01) has CRC 0x1021: lo=0x21, hi=0x10... but 0x10 is
    // DLE and 0x01 is SOH, so both payload and CRC high byte get escaped:
    // SOH DLE 01 21 DLE 10 EOT
    const QByteArray frame = Tink::buildFrame(QByteArray(1, Tink::CmdGetVer));
    QCOMPARE(frame, QByteArray::fromHex("01" "1001" "21" "1010" "04"));
}

void TestTink::frameEscapesControlBytes()
{
    // Payload bytes equal to SOH/EOT/DLE must be DLE-prefixed on the wire
    const QByteArray frame = Tink::buildFrame(QByteArray("\x03\x10\x01\x04", 4));

    // CRC of payload is 0xAB0A -> lo=0x0A, hi=0xAB (neither needs escaping)
    QCOMPARE(frame, QByteArray("\x01"          // SOH
                               "\x03"          // CmdWrite passes through
                               "\x10\x10"      // DLE escaped
                               "\x10\x01"      // SOH escaped
                               "\x10\x04"      // EOT escaped
                               "\x0a\xab"      // CRC lo, hi
                               "\x04", 11));   // EOT
}

void TestTink::frameEscapesControlBytesInCrc()
{
    // CmdErase (0x02) -> CRC 0x2042, no escapes at all
    const QByteArray frame = Tink::buildFrame(QByteArray(1, Tink::CmdErase));
    QCOMPARE(frame, QByteArray("\x01\x02\x42\x20\x04", 5));
}

void TestTink::rxAssemblesPacket()
{
    Tink tink;

    // A valid CmdErase response frame; state stays BlIdle so the packet
    // is CRC-checked but otherwise discarded without side effects
    feed(tink, QByteArray("\x01\x02\x42\x20\x04", 5));

    QCOMPARE(tink.m_rxState, Tink::RxIdle);
    QCOMPARE(tink.m_rxBuf, QByteArray("\x02\x42\x20", 3));
}

void TestTink::rxUnescapesControlBytes()
{
    Tink tink;

    // The TX frame for {03 10 01 04} decoded back must yield the
    // original payload + CRC with all escapes removed
    feed(tink, Tink::buildFrame(QByteArray("\x03\x10\x01\x04", 4)));

    QCOMPARE(tink.m_rxState, Tink::RxIdle);
    QCOMPARE(tink.m_rxBuf, QByteArray("\x03\x10\x01\x04\x0a\xab", 6));
}

void TestTink::rxIgnoresNoiseBeforeSoh()
{
    Tink tink;

    feed(tink, QByteArray("\xde\xad\xbe\xef", 4));      // line noise
    QCOMPARE(tink.m_rxState, Tink::RxIdle);

    feed(tink, QByteArray("\x01\x02\x42\x20\x04", 5));  // then a real frame
    QCOMPARE(tink.m_rxBuf, QByteArray("\x02\x42\x20", 3));
}

void TestTink::rxRejectsBadCrc()
{
    Tink tink;
    tink.m_blState = Tink::BlVersion; // pretend an update is running
    QSignalSpy finishedSpy(&tink, &Tink::finished);

    // Valid framing but corrupted CRC bytes
    feed(tink, QByteArray("\x01\x02\xff\xff\x04", 5));

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.takeFirst().at(0).toBool(), false);
    QCOMPARE(tink.m_blState, Tink::BlIdle); // state was reset
}

void TestTink::hexFileValid()
{
    QTemporaryFile file;
    Tink tink;
    tink.m_fwName = makeHexFile(file);

    QVERIFY(tink.validateHexFile());
    QCOMPARE(tink.m_hexNline, 2);
}

void TestTink::hexFileBadChecksum()
{
    QTemporaryFile file;
    file.open();
    file.write(":020000040000FB\n"); // checksum should be FA
    file.close();

    Tink tink;
    tink.m_fwName = file.fileName();
    QVERIFY(!tink.validateHexFile());
}

void TestTink::hexFileMissing()
{
    Tink tink;
    tink.m_fwName = QStringLiteral("/nonexistent/firmware.hex");
    QVERIFY(!tink.validateHexFile());
}

void TestTink::fullUpdateSession()
{
    // Simulate the device side of a complete update. The serial port is
    // closed, so TX goes nowhere (logged as failure), but every state
    // transition and progress report is exercised for real.
    QTemporaryFile file;
    Tink tink;
    tink.m_fwName = makeHexFile(file);
    QVERIFY(tink.validateHexFile()); // sets m_hexNline = 2

    QSignalSpy progressSpy(&tink, &Tink::progress);
    QSignalSpy writingSpy(&tink, &Tink::writingStarted);
    QSignalSpy finishedSpy(&tink, &Tink::finished);

    // Device answers the CmdGetVer probe with its ID string
    tink.m_blState = Tink::BlVersion;
    QByteArray verPayload(1, Tink::CmdGetVer);
    verPayload += QByteArray("RT4K\x00", 5);
    feed(tink, Tink::buildFrame(verPayload));
    QCOMPARE(tink.m_blState, Tink::BlErase);

    // Device acknowledges the erase; record 1/2 goes out, crossing the
    // 50% checkpoint -> progress(50)
    feed(tink, Tink::buildFrame(QByteArray(1, Tink::CmdErase)));
    QCOMPARE(tink.m_blState, Tink::BlWrite);
    QCOMPARE(tink.m_hexLine, 1);
    QCOMPARE(progressSpy.takeLast().at(0).toInt(), 50);
    QCOMPARE(writingSpy.count(), 1); // fired exactly once, at first frame

    // Device acknowledges record 1; record 2/2 goes out -> progress(100)
    feed(tink, Tink::buildFrame(QByteArray(1, Tink::CmdWrite)));
    QCOMPARE(tink.m_hexLine, 2);
    QCOMPARE(progressSpy.takeLast().at(0).toInt(), 100);

    // Device acknowledges record 2 -> JumpApp, done
    feed(tink, Tink::buildFrame(QByteArray(1, Tink::CmdWrite)));
    QCOMPARE(writingSpy.count(), 1); // still exactly once
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.takeFirst().at(0).toBool(), true);

    // Cleanup ran: firmware file closed, state machine back to idle
    QVERIFY(!tink.m_fwFile.isOpen());
    QCOMPARE(tink.m_blState, Tink::BlIdle);
    QVERIFY(!tink.isRunning());
}

QTEST_GUILESS_MAIN(TestTink)
#include "test_tink.moc"
