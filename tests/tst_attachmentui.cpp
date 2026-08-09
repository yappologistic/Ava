#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <memory>

class AttachmentUiTest final : public QObject
{
    Q_OBJECT

private:
    static QString qmlPath(const QString &name)
    {
        return QStringLiteral(AVA_SOURCE_DIR "/qml/chat/") + name;
    }

    static QVariantList attachments(int count)
    {
        QVariantList result;
        for (int index = 0; index < count; ++index) {
            result.append(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("image")},
                {QStringLiteral("name"), QStringLiteral("image-%1.png").arg(index + 1)},
                {QStringLiteral("previewUrl"),
                 QStringLiteral("file:///missing-image-%1.png").arg(index + 1)}
            });
        }
        return result;
    }

    static std::unique_ptr<QObject> create(QQmlEngine &engine,
                                            const QString &name,
                                            const QVariantMap &properties)
    {
        QQmlComponent component(&engine, QUrl::fromLocalFile(qmlPath(name)));
        if (!component.isReady())
            qWarning().noquote() << component.errorString();
        return std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
    }

private slots:
    void multiImageGalleryStaysBounded()
    {
        QQmlEngine engine;
        auto gallery = create(engine, QStringLiteral("CodexAttachmentGallery.qml"),
                              {{QStringLiteral("width"), 620},
                               {QStringLiteral("attachments"), attachments(7)},
                               {QStringLiteral("reducedMotion"), true}});
        QVERIFY(gallery);

        QCOMPARE(gallery->property("imageCount").toInt(), 7);
        QCOMPARE(gallery->property("galleryRows").toInt(), 2);
        QVERIFY(gallery->property("implicitHeight").toReal() <= 260.0);

        QSignalSpy inspectSpy(gallery.get(),
                              SIGNAL(inspectImageRequested(QString,QString)));
        QVERIFY(inspectSpy.isValid());
        QVERIFY(QMetaObject::invokeMethod(gallery.get(), "activate",
                                          Q_ARG(QVariant, QVariant(5))));
        QCOMPARE(inspectSpy.size(), 1);
        QCOMPARE(inspectSpy.constFirst().at(0).toString(),
                 QStringLiteral("file:///missing-image-6.png"));
        QCOMPARE(inspectSpy.constFirst().at(1).toString(),
                 QStringLiteral("image-6.png"));
    }

    void inspectorOpensClosesAndBoundsDecode()
    {
        QQmlEngine engine;
        auto inspector = create(engine, QStringLiteral("CodexImageInspector.qml"),
                                {{QStringLiteral("width"), 3840},
                                 {QStringLiteral("height"), 2160},
                                 {QStringLiteral("devicePixelRatio"), 2.0},
                                 {QStringLiteral("reducedMotion"), true}});
        QVERIFY(inspector);

        QVERIFY(QMetaObject::invokeMethod(
            inspector.get(), "openImage",
            Q_ARG(QVariant, QVariant(QStringLiteral("file:///missing-image.png"))),
            Q_ARG(QVariant, QVariant(QStringLiteral("missing-image.png")))));
        QCOMPARE(inspector->property("opened").toBool(), true);
        QCOMPARE(inspector->property("currentName").toString(),
                 QStringLiteral("missing-image.png"));

        auto *image = inspector->findChild<QQuickItem *>(QStringLiteral("inspectedImage"));
        QVERIFY(image);
        const QSize requested = image->property("sourceSize").toSize();
        QVERIFY(requested.width() <= 2560);
        QVERIFY(requested.height() <= 2560);

        QVERIFY(QMetaObject::invokeMethod(inspector.get(), "close"));
        QCOMPARE(inspector->property("opened").toBool(), false);
        QTRY_COMPARE_WITH_TIMEOUT(inspector->property("currentSource").toString(),
                                  QString(), 100);
    }
};

QTEST_MAIN(AttachmentUiTest)

#include "tst_attachmentui.moc"
