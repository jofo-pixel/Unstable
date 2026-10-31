/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTimer>
#include <QSlider>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QDebug>

#include <SDL2/SDL.h>

#include "types.h"
#include "Platform.h"

#include "InputConfigDialog.h"
#include "ui_InputConfigDialog.h"
#include "MapButton.h"


using namespace melonDS;
InputConfigDialog* InputConfigDialog::currentDlg = nullptr;

const int dskeyorder[12] = {0, 1, 10, 11, 5, 4, 6, 7, 9, 8, 2, 3};
const char* dskeylabels[12] = {"A", "B", "X", "Y", "Left", "Right", "Up", "Down", "L", "R", "Select", "Start"};

InputConfigDialog::InputConfigDialog(QWidget* parent) : QDialog(parent), ui(new Ui::InputConfigDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    emuInstance = ((MainWindow*)parent)->getEmuInstance();

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    for (int i = 0; i < keypad_num; i++)
    {
        const char* btn = EmuInstance::buttonNames[dskeyorder[i]];
        keypadKeyMap[i] = keycfg.GetInt(btn);
        keypadJoyMap[i] = joycfg.GetInt(btn);
    }

    int i = 0;
    for (int hotkey : hk_addons)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        addonsKeyMap[i] = keycfg.GetInt(btn);
        addonsJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    i = 0;
    for (int hotkey : hk_general)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        hkGeneralKeyMap[i] = keycfg.GetInt(btn);
        hkGeneralJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    i = 0;
    for (int hotkey : hk_popup)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        hkPopupKeyMap[i] = keycfg.GetInt(btn);
        hkPopupJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    populatePage(ui->tabAddons, hk_addons_labels, addonsKeyMap, addonsJoyMap);
    populatePage(ui->tabHotkeysGeneral, hk_general_labels, hkGeneralKeyMap, hkGeneralJoyMap);

    {
        // "Bottom screen" tab: popup + controller cursor, with a live joystick monitor
        QWidget* tabPopup = new QWidget();
        QVBoxLayout* vl = new QVBoxLayout(tabPopup);
        QWidget* mappings = new QWidget();
        populatePage(mappings, hk_popup_labels, hkPopupKeyMap, hkPopupJoyMap);
        vl->addWidget(mappings, 1);

        {
            QHBoxLayout* hl = new QHBoxLayout();
            QLabel* lbl = new QLabel("Cursor speed:");
            sldCursorSpeed = new QSlider(Qt::Horizontal);
            sldCursorSpeed->setRange(1, 10);
            sldCursorSpeed->setPageStep(1);
            sldCursorSpeed->setTickPosition(QSlider::TicksBelow);
            sldCursorSpeed->setTickInterval(1);
            sldCursorSpeed->setValue(instcfg.GetInt("PopupCursorSpeed"));
            QLabel* val = new QLabel(QString::number(sldCursorSpeed->value()));
            val->setMinimumWidth(20);
            connect(sldCursorSpeed, &QSlider::valueChanged, this, [this, val](int v)
            {
                val->setText(QString::number(v));
                emuInstance->popupCursorSpeed = v; // live preview while the dialog is open
            });
            hl->addWidget(lbl);
            hl->addWidget(sldCursorSpeed, 1);
            hl->addWidget(val);
            vl->addLayout(hl);
        }

        lblJoyMonitor = new QLabel();
        lblJoyMonitor->setWordWrap(true);
        lblJoyMonitor->setTextInteractionFlags(Qt::TextSelectableByMouse);
        vl->addWidget(lblJoyMonitor);

        ui->tabWidget->addTab(tabPopup, "Bottom screen");

        joyMonitorTimer = new QTimer(this);
        connect(joyMonitorTimer, &QTimer::timeout, this, &InputConfigDialog::updateJoyMonitor);
        joyMonitorTimer->start(100);
    }

    joystickID = instcfg.GetInt("JoystickID");

    int njoy = SDL_NumJoysticks();
    if (njoy > 0)
    {
        for (int i = 0; i < njoy; i++)
        {
            const char* name = SDL_JoystickNameForIndex(i);
            ui->cbxJoystick->addItem(QString(name));
        }
        ui->cbxJoystick->setCurrentIndex(joystickID);
    }
    else
    {
        ui->cbxJoystick->addItem("(no joysticks available)");
        ui->cbxJoystick->setEnabled(false);
    }

    setupKeypadPage();

    int inst = emuInstance->getInstanceID();
    if (inst > 0)
        ui->lblInstanceNum->setText(QString("Configuring mappings for instance %1").arg(inst+1));
    else
        ui->lblInstanceNum->hide();
}

InputConfigDialog::~InputConfigDialog()
{
    delete ui;
}

void InputConfigDialog::setupKeypadPage()
{
    for (int i = 0; i < keypad_num; i++)
    {
        QPushButton* pushButtonKey = this->findChild<QPushButton*>(QStringLiteral("btnKey") + dskeylabels[i]);
        QPushButton* pushButtonJoy = this->findChild<QPushButton*>(QStringLiteral("btnJoy") + dskeylabels[i]);

        KeyMapButton* keyMapButtonKey = new KeyMapButton(&keypadKeyMap[i], false);
        JoyMapButton* keyMapButtonJoy = new JoyMapButton(&keypadJoyMap[i], false);

        pushButtonKey->parentWidget()->layout()->replaceWidget(pushButtonKey, keyMapButtonKey);
        pushButtonJoy->parentWidget()->layout()->replaceWidget(pushButtonJoy, keyMapButtonJoy);

        delete pushButtonKey;
        delete pushButtonJoy;

        if (ui->cbxJoystick->isEnabled())
        {
            ui->stackMapping->setCurrentIndex(1);
        }
    }
}

void InputConfigDialog::populatePage(QWidget* page,
    const std::initializer_list<const char*>& labels,
    int* keymap, int* joymap)
{
    // kind of a hack
    bool ishotkey = (page != ui->tabInput);

    QHBoxLayout* main_layout = new QHBoxLayout();

    QGroupBox* group;
    QGridLayout* group_layout;

    group = new QGroupBox("Keyboard mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    int i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr)+":");
        KeyMapButton* btn = new KeyMapButton(&keymap[i], ishotkey);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    group = new QGroupBox("Joystick mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr)+":");
        JoyMapButton* btn = new JoyMapButton(&joymap[i], ishotkey);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    // wrap in a scroll area so long lists never push the dialog off screen
    QWidget* inner = new QWidget();
    inner->setLayout(main_layout);
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QVBoxLayout* pageLayout = new QVBoxLayout();
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(scroll);
    page->setLayout(pageLayout);
}

void InputConfigDialog::updateJoyMonitor()
{
    if (!lblJoyMonitor) return;

    auto mutex = getJoyMutex();
    SDL_LockMutex(mutex.get());
    SDL_Joystick* joy = getJoystick();

    QString text;
    if (!joy || !SDL_JoystickGetAttached(joy))
    {
        text = "Controller monitor: no controller connected.";
    }
    else
    {
        int nb = SDL_JoystickNumButtons(joy);
        int na = SDL_JoystickNumAxes(joy);
        int nh = SDL_JoystickNumHats(joy);

        QStringList pressed;
        for (int b = 0; b < nb; b++)
            if (SDL_JoystickGetButton(joy, b)) pressed << QString::number(b);
        for (int h = 0; h < nh; h++)
            if (SDL_JoystickGetHat(joy, h)) pressed << QString("Hat%1:%2").arg(h + 1).arg(SDL_JoystickGetHat(joy, h));

        QStringList axes;
        for (int a = 0; a < na; a++)
            axes << QString("%1:%2").arg(a + 1).arg(SDL_JoystickGetAxis(joy, a) / 327.67, 0, 'f', 0);

        text = QString("Controller monitor - %1 (%2 buttons, %3 axes, %4 hats)\n"
                       "Pressed: %5\nAxes (%): %6")
                   .arg(QString(SDL_JoystickName(joy)))
                   .arg(nb).arg(na).arg(nh)
                   .arg(pressed.isEmpty() ? QString("-") : pressed.join(" "))
                   .arg(axes.join("  "));
    }
    SDL_UnlockMutex(mutex.get());

    lblJoyMonitor->setText(text);
}

void InputConfigDialog::on_InputConfigDialog_accepted()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    for (int i = 0; i < keypad_num; i++)
    {
        const char* btn = EmuInstance::buttonNames[dskeyorder[i]];
        keycfg.SetInt(btn, keypadKeyMap[i]);
        joycfg.SetInt(btn, keypadJoyMap[i]);
    }

    int i = 0;
    for (int hotkey : hk_addons)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, addonsKeyMap[i]);
        joycfg.SetInt(btn, addonsJoyMap[i]);
        i++;
    }

    i = 0;
    for (int hotkey : hk_general)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, hkGeneralKeyMap[i]);
        joycfg.SetInt(btn, hkGeneralJoyMap[i]);
        i++;
    }

    i = 0;
    for (int hotkey : hk_popup)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, hkPopupKeyMap[i]);
        joycfg.SetInt(btn, hkPopupJoyMap[i]);
        i++;
    }

    if (sldCursorSpeed) instcfg.SetInt("PopupCursorSpeed", sldCursorSpeed->value());

    instcfg.SetInt("JoystickID", joystickID);
    Config::Save();

    emuInstance->inputLoadConfig();

    closeDlg();
}

void InputConfigDialog::on_InputConfigDialog_rejected()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    emuInstance->setJoystick(instcfg.GetInt("JoystickID"));
    emuInstance->popupCursorSpeed = instcfg.GetInt("PopupCursorSpeed"); // undo live preview

    closeDlg();
}

void InputConfigDialog::on_btnKeyMapSwitch_clicked()
{
    ui->stackMapping->setCurrentIndex(0);
}

void InputConfigDialog::on_btnJoyMapSwitch_clicked()
{
    ui->stackMapping->setCurrentIndex(1);
}

void InputConfigDialog::on_cbxJoystick_currentIndexChanged(int id)
{
    // prevent a spurious change
    if (ui->cbxJoystick->count() < 2) return;

    joystickID = id;
    emuInstance->setJoystick(id);
}

SDL_Joystick* InputConfigDialog::getJoystick()
{
    return emuInstance->getJoystick();
}

std::shared_ptr<SDL_mutex> InputConfigDialog::getJoyMutex()
{
    return emuInstance->getJoyMutex();
}
