#pragma once
#include <QString>
class QQmlApplicationEngine;
class Controller;
void runUiTest(QQmlApplicationEngine *, Controller *, const QString &, bool);

void runControllerTest(Controller *, const QString &);

void runQueueReuseTest(Controller *, const QString &);

void runFormatQueueTest(Controller *, const QString &);

void runSchedulingTest(Controller *, const QString &);
