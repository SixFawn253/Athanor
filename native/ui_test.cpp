#include "ui_test.h"
#include "controller.h"
#include "conversion.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QScreen>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QJSValue>
#include <QTimer>
#include <memory>
#include "platform.h"
static QString fixtureRoot()
{
    return qEnvironmentVariable("ATHANOR_TEST_FIXTURES", QCoreApplication::applicationDirPath() + "/../CompressionTest");
}
static QString testOutput(const QString &name)
{
    return QDir(qEnvironmentVariable("ATHANOR_TEST_OUTPUT_DIR", Platform::settingsDirectory() + "/tests")).filePath(name);
}
static QQuickItem *findVisualItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisualItem(child, name))
            return found;
    return nullptr;
}
void runUiTest(QQmlApplicationEngine *engine, Controller *controller, const QString &folder, bool compact)
{
    if (engine->rootObjects().isEmpty())
    {
        QCoreApplication::exit(2);
        return;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects()[0]);
    if (!window)
    {
        QCoreApplication::exit(2);
        return;
    }
    controller->setAppearance("Dark");
    controller->setMotion("Full");
    controller->setOption("mode", "quality");
    window->raise();
    window->requestActivate();
    if (compact && qgetenv("ATHANOR_UI_CATEGORY") == "pdf")
        controller->setOption("pdf_output", "jpg");
    QDir().mkpath(folder);
    auto result = std::make_shared<QJsonObject>();
    auto error = std::make_shared<QString>();
    QObject::connect(engine, &QQmlEngine::warnings, controller, [error](const QList<QQmlError> &errors) {
        for (const auto &e : errors)
            *error += e.toString() + "\n";
    });
    auto capture = [window, folder, error, result](QString name) {
        auto image = window->grabWindow();
        if (image.isNull() || !image.save(folder + '/' + name + ".png"))
            *error += "Render capture failed: " + name + "\n";
        QJsonObject state;
        for (auto label :
             {"pages", "convertPage", "transitionCover", "queueCard", "queueHeader", "outputCard", "savePath",
              "browseButton", "progressCell0", "progressTrack0", "progressLabel0", "statusGlyph0"})
        {
            auto *item = findVisualItem(window->contentItem(), label);
            if (item)
            {
                QJsonObject info;
                for (auto property : {"visible", "x", "y", "width", "height", "currentIndex", "progress", "available"})
                    if (item->property(property).isValid())
                        info[property] = QJsonValue::fromVariant(item->property(property));
                state[label] = info;
            }
        }
        (*result)[name] = state;
    };
    auto end = [folder, result, error] {
        (*result)["ok"] = error->isEmpty();
        (*result)["errors"] = *error;
        QFile f(folder + "/ui-result.json");
        f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(*result).toJson());
        f.close();
        QCoreApplication::exit(error->isEmpty() ? 0 : 1);
    };
    QVector<QueueItem> rows;
    for (int i = 0; i < (compact ? 1 : 413); i++)
    {
        QueueItem row;
        row.source = fixtureRoot() + "/Apple_first_logo.png";
        row.before = QFileInfo(row.source).size();
        if (i % 2)
        {
            row.after = row.before / 2;
            row.status = "Done";
            row.progress = 100;
        }
        row.category = compact && !qgetenv("ATHANOR_UI_CATEGORY").isEmpty()
                           ? QString::fromUtf8(qgetenv("ATHANOR_UI_CATEGORY"))
                           : QString("image");
        if (!(i % 2))
        {
            row.status = "Queued";
            row.progress = i % 101;
        }
        rows.append(row);
    }
    controller->queue()->add(rows);
    emit controller->changed();
    auto phase = std::make_shared<std::function<void(int)>>();
    *phase = [=](int step) {
        int delay = 300;
        switch (step)
        {
        case 0:
            capture(compact ? "quick-dark" : "convert-dark");
            (*result)["queue_count"] = controller->count();
            if (compact)
            {
                controller->setOption("mode", "target");
                QTimer::singleShot(300, controller, [=] {
                    capture("quick-target");
                    if (result->value("quick-dark").toObject().value("outputCard") !=
                        result->value("quick-target").toObject().value("outputCard"))
                        *error += "Quick target mode shifted settings\n";
                    end();
                });
                return;
            }
            QMetaObject::invokeMethod(window->findChild<QObject *>("settingsButton"), "clicked");
            delay = 180;
            break;
        case 1:
            capture("reveal-middle");
            delay = 450;
            break;
        case 2:
            capture("settings-dark");
            if (window->property("page").toInt() != 1)
                *error += "Settings switch failed\n";
            controller->setAppearance("Light");
            break;
        case 3:
            capture("settings-light");
            QMetaObject::invokeMethod(window->findChild<QObject *>("backButton"), "clicked");
            delay = 600;
            break;
        case 4:
            capture("convert-light");
            controller->setOption("mode", "target");
            {
                auto *cover = window->findChild<QObject *>("transitionCover");
                if (cover && cover->property("visible").toBool())
                    *error += "Transition cover did not finish\n";
            }
            controller->clear();
            break;
        case 5:
            capture("target-light");
            {
                auto before = result->value("convert-light").toObject();
                auto after = result->value("target-light").toObject();
                for (auto label : {"queueCard", "outputCard"})
                {
                    if (before.value(label) != after.value(label))
                        *error += QString("Target mode shifts %1\n").arg(label);
                }
                auto card = window->findChild<QObject *>("queueCard");
                auto header = window->findChild<QObject *>("queueHeader");
                if (card && header && card->property("width") != header->property("width"))
                    *error += "Queue header width mismatch\n";
            }
            controller->setOption("mode", "convert");
            break;
        case 6:
            capture("convert-mode");
            if (!controller->options().value("convert_only").toBool() ||
                controller->options().value("size_mode").toBool())
                *error += "Convert mode state failed\n";
            if (result->value("convert-mode").toObject().value("outputCard") !=
                result->value("target-light").toObject().value("outputCard"))
                *error += "Convert mode shifts output controls\n";
            controller->setOption("mode", "quality");
            {
                auto path = window->findChild<QObject *>("savePath");
                auto browse = window->findChild<QObject *>("browseButton");
                if (path && browse && path->property("height") != browse->property("height"))
                    *error += "Save path height mismatch\n";
                auto field = window->findChild<QObject *>("imageFormat");
                if (field)
                {
                    auto *badge = findVisualItem(qobject_cast<QQuickItem *>(field), "transparencyBadge");
                    if (!badge || badge->x() < field->property("width").toDouble() / 2)
                        *error += "Transparency icon alignment failed\n";
                    auto popup = field->property("popup").value<QObject *>();
                    if (popup)
                        QMetaObject::invokeMethod(popup, "open");
                }
            }
            break;
        case 7:
            capture("dropdown-light");
            {
                auto field = window->findChild<QObject *>("imageFormat");
                if (field)
                {
                    auto popup = field->property("popup").value<QObject *>();
                    if (!popup || !popup->property("visible").toBool())
                        *error += "Dropdown did not open\n";
                    else
                    {
                        auto *content = qobject_cast<QQuickItem *>(popup->property("contentItem").value<QObject *>());
                        // Native popup windows may be clamped to a small desktop.
                        // Keep checking centering whenever it fits, and require a
                        // clamped popup to remain completely on the same screen.
                        const auto expectedY = (field->property("height").toDouble() - popup->property("height").toDouble()) / 2;
                        if (qAbs(popup->property("y").toDouble() - expectedY) > 1 &&
                            (!content || !content->window() ||
                             !window->screen()->availableGeometry().adjusted(-2, -2, 2, 2).contains(content->window()->frameGeometry())))
                            *error += "Dropdown is misaligned or outside the available desktop\n";
                        if (content && content->window())
                        {
                            auto *popupWindow = content->window();
                            popupWindow->grabWindow().save(folder + "/dropdown-popup-light.png");
                            // Exercise the actual popup delegate through this app's own window.
                            QPointF pos = content->mapToScene(QPointF(80, 60));
                            QMouseEvent press(QEvent::MouseButtonPress, pos, popupWindow->mapToGlobal(pos.toPoint()),
                                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                            QCoreApplication::sendEvent(popupWindow, &press);
                            QMouseEvent release(QEvent::MouseButtonRelease, pos,
                                                popupWindow->mapToGlobal(pos.toPoint()), Qt::LeftButton, Qt::NoButton,
                                                Qt::NoModifier);
                            QCoreApplication::sendEvent(popupWindow, &release);
                        }
                        QMetaObject::invokeMethod(popup, "close");
                    }
                }
            }
            break;
        case 8: {
            auto *spin = window->findChild<QQuickItem *>("qualitySpin");
            if (spin)
            {
                spin->forceActiveFocus(Qt::TabFocusReason);
                QMetaObject::invokeMethod(window, "revealFocus", Q_ARG(QVariant, QVariant::fromValue(spin)));
                QTimer::singleShot(90, controller, [=] {
                    int before = spin->property("value").toInt();
                    auto pos = spin->mapToScene(QPointF(spin->width() - 14, spin->height() / 2));
                    QMouseEvent press(QEvent::MouseButtonPress, pos, window->mapToGlobal(pos.toPoint()), Qt::LeftButton,
                                      Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &press);
                    QMouseEvent release(QEvent::MouseButtonRelease, pos, window->mapToGlobal(pos.toPoint()),
                                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &release);
                    if (spin->property("value").toInt() != before + 1)
                    {
                        QJsonObject trace;
                        trace["x"] = pos.x();
                        trace["y"] = pos.y();
                        trace["active"] = spin->hasActiveFocus();
                        trace["enabled"] = spin->isEnabled();
                        auto scroll = window->findChild<QObject *>("panelsScroll");
                        if (scroll)
                        {
                            trace["scroll_y"] = scroll->property("contentY").toDouble();
                            trace["scroll_height"] = scroll->property("height").toDouble();
                        }
                        (*result)["quality_trace"] = trace;
                        *error += "Quality increment failed\n";
                    }
                });
            }
        }
            if (controller->options().value("image").toString() != "webp")
                *error += "Dropdown selection failed\n";
            controller->setAppearance("Dark");
            {
                auto *button = window->findChild<QQuickItem *>("addFilesButton");
                if (button)
                {
                    auto pos = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
                    QMouseEvent move(QEvent::MouseMove, pos, window->mapToGlobal(pos.toPoint()), Qt::NoButton,
                                     Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &move);
                }
            }
            break;
        case 9:
            capture("hover-dark");
            {
                auto *tab = findVisualItem(window->contentItem(), "modeTab0");
                if (tab)
                {
                    tab->forceActiveFocus(Qt::TabFocusReason);
                    QTimer::singleShot(80, controller, [=] {
                        auto pos = tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2));
                        QMouseEvent move(QEvent::MouseMove, pos, window->mapToGlobal(pos.toPoint()), Qt::NoButton,
                                         Qt::NoButton, Qt::NoModifier);
                        QCoreApplication::sendEvent(window, &move);
                        window->grabWindow();
                    });
                }
                else
                    *error += "Mode tab not found\n";
            }
            break;
        case 10: {
            auto *reveal = findVisualItem(window->contentItem(), "tabReveal0");
            if (reveal)
            {
                auto *animation =
                    findVisualItem(window->contentItem(), "modeTab0")->findChild<QObject *>("tabHoverAnimation0");
                // Constructed mouse events do not update the desktop cursor. Test
                // the started animation and its final geometry independently of
                // subsequent platform hover events during render capture.
                if (animation && animation->property("running").toBool())
                {
                    (*result)["hover_animation_started"] = true;
                    QMetaObject::invokeMethod(animation, "complete");
                }
            }
        }
            capture("tab-hover-dark");
            {
                auto reveal = findVisualItem(window->contentItem(), "tabReveal0");
                if (!reveal || reveal->property("width").toDouble() < 100)
                {
                    auto *tab = findVisualItem(window->contentItem(), "modeTab0");
                    auto *mouse = findVisualItem(window->contentItem(), "tabMouse0");
                    auto *scroll = window->findChild<QObject *>("panelsScroll");
                    QJsonObject trace;
                    if (tab)
                    {
                        auto pos = tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2));
                        trace["center_x"] = pos.x();
                        trace["center_y"] = pos.y();
                        trace["visible"] = tab->isVisible();
                        auto hover = tab->findChild<QObject *>("tabPointer0");
                        if (hover)
                            trace["handler_hovered"] = hover->property("hovered").toBool();
                    }
                    if (mouse)
                    {
                        trace["mouse_hovered"] = mouse->property("containsMouse").toBool();
                        trace["mouse_width"] = mouse->width();
                        trace["mouse_height"] = mouse->height();
                    }
                    if (scroll)
                        trace["scroll_y"] = scroll->property("contentY").toDouble();
                    auto point = window->property("pointerPosition").toPointF();
                    trace["pointer_x"] = point.x();
                    trace["pointer_y"] = point.y();
                    if (reveal)
                    {
                        trace["reveal_width"] = reveal->width();
                        auto animation = findVisualItem(window->contentItem(), "modeTab0")
                                             ->findChild<QObject *>("tabHoverAnimation0");
                        if (animation)
                        {
                            trace["animation_running"] = animation->property("running").toBool();
                            trace["animation_duration"] = animation->property("duration").toInt();
                        }
                        if (reveal->parentItem())
                        {
                            trace["parent_width"] = reveal->parentItem()->width();
                            trace["parent_class"] = reveal->parentItem()->metaObject()->className();
                        }
                    }
                    trace["window_exposed"] = window->isExposed();
                    trace["window_active"] = window->isActive();
                    trace["window_height"] = window->height();
                    (*result)["hover_trace"] = trace;
                    *error += "Tab circle reveal did not expand\n";
                }
                auto button = window->findChild<QObject *>("addFilesButton");
                if (!button || button->property("height").toDouble() != 52)
                    *error += "Toolbar size mismatch\n";
            }
            {
                QVector<QueueItem> samples;
                for (int i = 0; i < 3; i++)
                {
                    QueueItem row;
                    row.source = fixtureRoot() + "/Apple_first_logo.png";
                    row.before = QFileInfo(row.source).size();
                    row.category = "image";
                    row.status = i == 2 ? "Error" : "Done";
                    row.progress = i == 2 ? 20 : 100;
                    row.after = i == 2 ? -1 : row.before / 2;
                    if (i < 2)
                        row.output = row.source;
                    if (i == 1)
                        row.warning = "Target too small; saved the smallest valid result.";
                    if (i == 2)
                        row.warning = "Sample failure: choose an output folder.";
                    samples.append(row);
                }
                controller->queue()->add(samples);
                emit controller->changed();
                auto *panel = window->findChild<QObject *>("queueCard");
                if (panel)
                {
                    auto original = panel->property("widths").value<QJSValue>();
                    QMetaObject::invokeMethod(panel, "resizeColumn", Q_ARG(QVariant, QVariant(0)),
                                              Q_ARG(QVariant, QVariant(35)),
                                              Q_ARG(QVariant, QVariant::fromValue(original)));
                    window->setProperty("selectedRow", 1);
                }
                auto summary = controller->queueSummary();
                if (summary.value("failed").toInt() != 1 || summary.value("done").toInt() != 2 ||
                    summary.value("warnings").toInt() != 1)
                    *error += "Unexpected fixture queue errors\n";
                auto *tab = findVisualItem(window->contentItem(), "modeTab0");
                if (tab)
                {
                    tab->forceActiveFocus(Qt::TabFocusReason);
                    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &right);
                    if (!controller->options().value("size_mode").toBool())
                        *error += "Keyboard mode navigation failed\n";
                    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
                    QCoreApplication::sendEvent(window, &left);
                }
            }
            window->resize(900, 780);
            delay = 300;
            break;
        case 11:
            capture("compact-dark");
            window->resize(900, 600);
            controller->setMotion("Reduced");
            QMetaObject::invokeMethod(window->findChild<QObject *>("settingsButton"), "clicked");
            delay = 300;
            break;
        case 12:
            capture("reduced-motion-settings");
            {
                auto cover = window->findChild<QObject *>("transitionCover");
                if (cover && cover->property("visible").toBool())
                    *error += "Reduced motion still animates a transition\n";
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &escape);
                if (window->property("page").toInt() != 0)
                    *error += "Escape navigation failed\n";
                controller->setOption("output", QCoreApplication::applicationFilePath());
                if (controller->outputError().isEmpty())
                    *error += "File path accepted as output folder\n";
                controller->setOption("output", QString());
            }
            QTimer::singleShot(100, controller, [=] {
                capture("short-window-dark");
                auto button = window->findChild<QQuickItem *>("convertButton");
                if (!button || button->mapToScene(QPointF(0, button->height())).y() > window->height())
                    *error += "Conversion button obscured in short window\n";
                {
                    auto preview = window->findChild<QObject *>("previewDialog");
                    if (preview)
                    {
                        QString fixture = QUrl::fromLocalFile(fixtureRoot() + "/Apple_first_logo.png")
                                              .toString();
                        preview->setProperty("sources", QVariantList{fixture, fixture});
                    }
                    if (preview)
                        QMetaObject::invokeMethod(preview, "showPreview", Q_ARG(QVariant, QVariant("image")),
                                                  Q_ARG(QVariant, QVariant(50)));
                }
            });
            delay = 300;
            break;
        case 13:
            window->resize(1180, 820);
            capture("preview-dark");
            {
                auto preview = window->findChild<QObject *>("previewDialog");
                if (!preview || !preview->property("visible").toBool())
                    *error += "Preview dialog did not open\n";
                if (preview)
                {
                    preview->setProperty("zoom", 2.0);
                    preview->setProperty("panX", .5);
                    preview->setProperty("panY", .3);
                }
            }
            break;
        case 14:
            capture("preview-zoom-dark");
            {
                auto preview = window->findChild<QObject *>("previewDialog");
                if (preview)
                    QMetaObject::invokeMethod(preview, "close");
            }
            controller->setMotion("System");
            break;
        case 15: {
            auto guide = window->findChild<QObject *>("formatGuide");
            if (!guide)
                *error += "Format guide missing\n";
            else
                QMetaObject::invokeMethod(guide, "openFormat", Q_ARG(QVariant, QVariant("opus")));
        }
        break;
        case 16:
            capture("format-guide-dark");
            {
                auto guide = window->findChild<QObject *>("formatGuide");
                if (!guide || !guide->property("visible").toBool() || guide->property("category").toString() != "Audio")
                    *error += "Format info failed\n";
                if (guide)
                    QMetaObject::invokeMethod(guide, "close");
                auto check = window->findChild<QObject *>("deleteOriginals");
                auto prompts = window->findChild<QObject *>("conversionPrompts");
                if (check && prompts)
                {
                    check->setProperty("checked", true);
                    QMetaObject::invokeMethod(prompts, "toggleDelete", Q_ARG(QVariant, QVariant::fromValue(check)));
                }
            }
            break;
        case 17:
            capture("delete-warning-dark");
            {
                auto dialog = window->findChild<QObject *>("deleteWarning");
                if (!dialog || !dialog->property("visible").toBool() || controller->options().value("delete").toBool())
                    *error += "Delete confirmation failed\n";
                if (dialog)
                {
                    QMetaObject::invokeMethod(dialog, "close");
                    QMetaObject::invokeMethod(dialog, "acceptedAction");
                }
            }
            break;
        case 18:
            capture("delete-checked-dark");
            if (!controller->options().value("delete").toBool())
                *error += "Delete confirmation not applied\n";
            controller->setOption("delete", false);
            controller->setOption("image", "jpg");
            {
                auto prompts = window->findChild<QObject *>("conversionPrompts");
                if (prompts)
                    QMetaObject::invokeMethod(prompts, "start");
            }
            break;
        case 19:
            capture("transparency-warning-dark");
            {
                auto dialog = window->findChild<QObject *>("conversionWarning");
                if (!dialog || !dialog->property("visible").toBool() || controller->busy())
                    *error += "Transparency warning did not precede conversion\n";
                if (dialog)
                    QMetaObject::invokeMethod(dialog, "close");
                auto input = window->findChild<QQuickItem *>("savePath"),
                     browse = window->findChild<QQuickItem *>("browseButton");
                if (!input || !browse ||
                    std::abs(input->mapToScene(QPointF(input->width(), 0)).x() - browse->mapToScene(QPointF()).x()) >
                        .5)
                    *error += "Save path and Browse are not joined\n";
            }
            controller->setOption("image", "avif");
            break;
        default:
            QMetaObject::invokeMethod(window->findChild<QObject *>("helpButton"), "clicked");
            {
                auto dialog = window->findChild<QObject *>("helpDialog");
                if (!dialog || !dialog->property("visible").toBool())
                    *error += "Help icon failed\n";
                if (dialog)
                    QMetaObject::invokeMethod(dialog, "close");
            }
            controller->setOption("mode", "quality");
            controller->setAppearance("Dark");
            end();
            return;
        }
        QTimer::singleShot(delay, controller, [phase, step] { (*phase)(step + 1); });
    };
    QTimer::singleShot(500, controller, [phase] { (*phase)(0); });
}

void runControllerTest(Controller *controller, const QString &folder)
{
    auto phase = std::make_shared<int>(0);
    auto finished = std::make_shared<bool>(false);
    auto errors = std::make_shared<QString>();
    auto end = [=] {
        if (*finished)
            return;
        *finished = true;
        controller->closePreview();
        Conversion::writeLine({{"ok", errors->isEmpty()}, {"error", *errors}});
        QCoreApplication::exit(errors->isEmpty() ? 0 : 1);
    };
    controller->setOption("output", testOutput("feature-controller"));
    controller->setOption("image", "avif");
    controller->setOption("threads", 2);
    controller->setOption("speed", "Fast");
    controller->setOption("delete", false);
    controller->setOption("size_mode", false);
    QObject::connect(controller, &Controller::filesAdded, controller, [=] {
        if (*phase == 0)
        {
            *phase = 1;
            controller->start();
        }
        else if (*phase == 3)
        {
            *phase = 5;
            controller->preview("video", 32, 0);
        }
    });
    QObject::connect(controller, &Controller::batchFinished, controller, [=](int success, int failed, int cancelled) {
        if (*phase == 1)
        {
            if (success != 3 || failed || cancelled)
                *errors += "Batch counts incorrect; ";
            for (const auto &row : controller->queue()->items)
                if (row.status != "Done" || row.progress != 100 || !QFileInfo::exists(row.output))
                    *errors += "Batch output missing; ";
            for (const auto &row : controller->queue()->items)
                if (row.before != QFileInfo(row.source).size() || row.after != QFileInfo(row.output).size())
                    *errors += "Queue sizes incorrect; ";
            if (controller->queue()->data(controller->queue()->index(0), QueueModel::Smaller).toString() ==
                QString::fromUtf8("—"))
                *errors += "Savings column missing; ";
            *phase = 2;
            controller->preview("image", 50, 0);
        }
        else if (*phase == 4)
        {
            if (!cancelled || failed)
                *errors += "Cancellation counts incorrect; ";
            QTimer::singleShot(300, controller, [=] {
                QDir out(testOutput("feature-controller"));
                if (!out.entryList({".athanor-job-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())
                    *errors += "Cancelled staging was not removed; ";
                end();
            });
        }
    });
    QObject::connect(controller, &Controller::previewChanged, controller, [=] {
        if ((*phase == 2 || *phase == 5) && !controller->previewBusy() &&
            (!controller->previewError().isEmpty() || !controller->previewResult().isEmpty()))
        {
            if (!controller->previewError().isEmpty() || controller->previewResult().isEmpty())
                *errors += "Preview failed; ";
            if (*phase == 5)
            {
                *phase = 4;
                controller->closePreview();
                controller->start();
                QTimer::singleShot(250, controller, &Controller::cancel);
                return;
            }
            *phase = 3;
            controller->closePreview();
            controller->clear();
            controller->addPaths({folder + "/Recording 2026-10-06 070218.mp4"});
        }
    });
    QTimer::singleShot(120000, controller, [=] {
        *errors += "Test timed out; ";
        controller->cancel();
        end();
    });
    controller->addUrls({QUrl::fromLocalFile(folder + "/Apple_first_logo.png"),
                         QUrl::fromLocalFile(folder + "/astrid.gif"),
                         QUrl::fromLocalFile(folder + "/willowshore-waldgeist.webp")});
}

void runQueueReuseTest(Controller *controller, const QString &fixtures)
{
    auto phase = std::make_shared<int>(0);
    auto changed = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    auto end = [=] {
        Conversion::writeLine({{"ok", error->isEmpty()}, {"error", *error}});
        QCoreApplication::exit(error->isEmpty() ? 0 : 1);
    };
    controller->setOption("output", testOutput("queue-check"));
    controller->setOption("speed", "Fast");
    controller->setOption("threads", 2);
    controller->setOption("batch_workers", 1);
    controller->setOption("size_mode", false);
    controller->setOption("image_lossless", false);
    controller->setOption("delete", false);
    controller->setOption("image", "avif");
    QObject::connect(controller, &Controller::filesAdded, controller, [=] {
        if (*phase == 0)
        {
            *phase = 1;
            controller->start();
            controller->start();
            controller->addPaths({fixtures + "/willowshore-waldgeist.webp"});
        }
        else if (*phase == 1)
            *changed = true;
    });
    QObject::connect(controller, &Controller::batchFinished, controller, [=](int success, int failed, int cancelled) {
        if (*phase == 1)
        {
            if (!*changed || success != 2 || failed || cancelled || controller->count() != 2)
                *error += "Adding during batch failed; ";
            if (controller->canConvert())
                *error += "Completed batch remains runnable without a change; ";
            *phase = 2;
            controller->setOption("image", "webp");
            if (!controller->canConvert())
                *error += "Format change did not enable reconversion; ";
            controller->start();
        }
        else if (*phase == 2)
        {
            if (success != 2 || failed || cancelled || controller->count() != 2)
                *error += "Reuse failed; ";
            for (const auto &row : controller->queue()->items)
                if (QFileInfo(row.output).suffix() != "webp")
                    *error += "Wrong output extension; ";
            QueueItem previous = controller->queue()->items[0];
            controller->clear();
            previous.source = testOutput("queue-check/original-no-longer-present.png");
            controller->queue()->add({previous});
            *phase = 3;
            controller->setOption("image", "avif");
            controller->start();
        }
        else if (*phase == 3)
        {
            if (success != 1 || failed || cancelled ||
                QFileInfo(controller->queue()->items[0].output).fileName() !=
                    "original-no-longer-present_athanor-compressed.avif")
                *error += "Previous output fallback failed; ";
            end();
        }
    });
    QTimer::singleShot(30000, controller, [=] {
        *error += "Queue test timed out; ";
        controller->cancel();
        end();
    });
    controller->addPaths({fixtures + "/Apple_first_logo.png"});
}

void runFormatQueueTest(Controller *controller, const QString &fixtures)
{
    auto finished = std::make_shared<bool>(false);
    auto end = [=](const QString &error) {
        if (*finished)
            return;
        *finished = true;
        Conversion::writeLine({{"ok", error.isEmpty()}, {"error", error}});
        QCoreApplication::exit(error.isEmpty() ? 0 : 1);
    };
    controller->setOption("delete", false);
    controller->setOption("output", testOutput("format-queue-check"));
    controller->setOption("mode", "convert");
    controller->setOption("image", "pdf");
    QObject::connect(controller, &Controller::filesAdded, controller, [=] {
        if (controller->count() != 2)
        {
            end("Queue did not load both image fixtures");
            return;
        }
        controller->start();
        controller->start();
    });
    QObject::connect(controller, &Controller::batchFinished, controller, [=](int success, int failed, int cancelled) {
        if (success != 2 || failed || cancelled)
        {
            end("Combined PDF queue completion counts failed");
            return;
        }
        auto rows = controller->queue()->items;
        if (rows.size() != 2 || rows[0].status != "Done" || rows[1].status != "Done" ||
            rows[0].output != rows[1].output)
        {
            end("Combined PDF queue rows did not share one result");
            return;
        }
        if (rows[0].after + rows[1].after != QFileInfo(rows[0].output).size())
        {
            end("Combined PDF queue double-counted result size");
            return;
        }
        if (controller->canConvert())
        {
            end("Completed PDF batch remained eligible without a setting change");
            return;
        }
        controller->setOption("image", "png");
        if (!controller->canConvert())
        {
            end("Completed rows cannot be converted to another image format");
            return;
        }
        end({});
    });
    controller->addPaths({fixtures + "/willowshore-waldgeist.webp", fixtures + "/artificial.jpg"});
    QTimer::singleShot(20000, controller, [=] { end("Combined PDF queue timed out"); });
}
