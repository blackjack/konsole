/*
    Copyright 2007-2008 by Robert Knight <robertknight@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/

// Own
#include "Part.h"

// Qt
#include <QtCore/QStringList>
#include <QtCore/QDir>
#include <QtGui/QKeyEvent>
#include <QtCore/QUrl>

// KDE
#include <QAction>
#include <KActionCollection>
#include <KPluginFactory>
#include <QDebug>

// Konsole
#include "EditProfileDialog.h"
#include "Emulation.h"
#include "Session.h"
#include "SessionController.h"
#include "SessionManager.h"
#include "ProfileManager.h"
#include "TerminalDisplay.h"
#include "ViewManager.h"

using namespace Konsole;

K_PLUGIN_FACTORY(KonsolePartFactory, registerPlugin<Konsole::Part>();)
K_EXPORT_PLUGIN(KonsolePartFactory("konsole"))

Part::Part(QWidget* parentWidget , QObject* parent, const QVariantList&)
    : KParts::ReadOnlyPart(parent)
    , _viewManager(0)
    , _pluggedController(0)
    , _manageProfilesAction(0)
{
    // create view widget
    _viewManager = new ViewManager(this, actionCollection());

    connect(_viewManager, &Konsole::ViewManager::activeViewChanged, this , &Konsole::Part::activeViewChanged);
    connect(_viewManager, &Konsole::ViewManager::empty, this, &Konsole::Part::terminalExited);
    connect(_viewManager, static_cast<void(ViewManager::*)()>(&Konsole::ViewManager::newViewRequest), this, &Konsole::Part::newTab);

    _viewManager->widget()->setParent(parentWidget);

    setWidget(_viewManager->widget());
    actionCollection()->addAssociatedWidget(_viewManager->widget());
    foreach(QAction* action, actionCollection()->actions()) {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

    // Enable translucency support.
    _viewManager->widget()->setAttribute(Qt::WA_TranslucentBackground, true);

    // create basic session
    createSession();
}

Part::~Part()
{
    ProfileManager::instance()->saveSettings();
    delete _viewManager;
}

bool Part::openFile()
{
    return false;
}

void Part::terminalExited()
{
    deleteLater();
}

void Part::newTab()
{
    createSession();
}

Session* Part::activeSession() const
{
    if (_viewManager->activeViewController()) {
        Q_ASSERT(_viewManager->activeViewController()->session());

        return _viewManager->activeViewController()->session();
    } else {
        return 0;
    }
}
void Part::startProgram(const QString& program,
                        const QStringList& arguments)
{
    Q_ASSERT(activeSession());

    // do nothing if the session has already started running
    if (activeSession()->isRunning())
        return;

    if (!program.isEmpty() && !arguments.isEmpty()) {
        activeSession()->setProgram(program);
        activeSession()->setArguments(arguments);
    }

    activeSession()->run();
}

void Part::openTeletype(int fd)
{
    Q_ASSERT(activeSession());

    activeSession()->openTeletype(fd);
}

void Part::showShellInDir(const QString& dir)
{
    Q_ASSERT(activeSession());

    // do nothing if the session has already started running
    if (activeSession()->isRunning())
        return;

    if (!dir.isEmpty())
        activeSession()->setInitialWorkingDirectory(dir);

    activeSession()->run();
}

void Part::sendInput(const QString& text)
{
    Q_ASSERT(activeSession());
    activeSession()->sendText(text);
}

int Part::terminalProcessId()
{
    Q_ASSERT(activeSession());

    return activeSession()->processId();
}

int Part::foregroundProcessId()
{
    Q_ASSERT(activeSession());

    if (activeSession()->isForegroundProcessActive()) {
        return activeSession()->foregroundProcessId();
    } else {
        return -1;
    }
}

QString Part::foregroundProcessName()
{
    Q_ASSERT(activeSession());

    if (activeSession()->isForegroundProcessActive()) {
        return activeSession()->foregroundProcessName();
    } else {
        return "";
    }
}

QString Part::currentWorkingDirectory() const
{
    Q_ASSERT(activeSession());

    return activeSession()->currentWorkingDirectory();
}

void Part::createSession(const QString& profileName, const QString& directory)
{
    Profile::Ptr profile = ProfileManager::instance()->defaultProfile();
    if (!profileName.isEmpty())
        profile = ProfileManager::instance()->loadProfile(profileName);

    Q_ASSERT(profile);

    Session* session = SessionManager::instance()->createSession(profile);

    // override the default directory specified in the profile
    if (!directory.isEmpty() && profile->startInCurrentSessionDir())
        session->setInitialWorkingDirectory(directory);

    _viewManager->createView(session);
}

QStringList Part::profileNameList() const
{
    return ProfileManager::instance()->availableProfileNames();
}

void Part::activeViewChanged(SessionController* controller)
{
    Q_ASSERT(controller);
    Q_ASSERT(controller->view());

    // remove existing controller
    if (_pluggedController) {
        removeChildClient(_pluggedController);
        disconnect(_pluggedController, &Konsole::SessionController::titleChanged, this,
                   &Konsole::Part::activeViewTitleChanged);
        disconnect(_pluggedController, &Konsole::SessionController::currentDirectoryChanged, this,
                   &Konsole::Part::currentDirectoryChanged);
    }

    // insert new controller
    insertChildClient(controller);

    connect(controller, &Konsole::SessionController::titleChanged, this, &Konsole::Part::activeViewTitleChanged);
    activeViewTitleChanged(controller);
    connect(controller, &Konsole::SessionController::currentDirectoryChanged, this, &Konsole::Part::currentDirectoryChanged);

    const char* displaySignal = SIGNAL(overrideShortcutCheck(QKeyEvent*,bool&));
    const char* partSlot = SLOT(overrideTerminalShortcut(QKeyEvent*,bool&));

    disconnect(controller->view(), displaySignal, this, partSlot);
    connect(controller->view(), displaySignal, this, partSlot);

    // set the current session's search bar
    controller->setSearchBar(_viewManager->searchBar());

    _pluggedController = controller;
}

void Part::overrideTerminalShortcut(QKeyEvent* event, bool& override)
{
    // Shift+Insert is commonly used as the alternate shortcut for
    // pasting in KDE apps(including konsole), so it deserves some
    // special treatment.
    if ((event->modifiers() & Qt::ShiftModifier) &&
            (event->key() == Qt::Key_Insert)) {
        override = false;
        return;
    }

    // override all shortcuts in the embedded terminal by default
    override = true;
    emit overrideShortcut(event, override);
}

void Part::activeViewTitleChanged(ViewProperties* properties)
{
    emit setWindowCaption(properties->title());
}

void Part::showEditCurrentProfileDialog(QWidget* parent)
{
    Q_ASSERT(activeSession());

    EditProfileDialog* dialog = new EditProfileDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProfile(SessionManager::instance()->sessionProfile(activeSession()));
    dialog->show();
}

void Part::changeSessionSettings(const QString& text)
{
    Q_ASSERT(activeSession());

    // send a profile change command, the escape code format
    // is the same as the normal X-Term commands used to change the window title or icon,
    // but with a magic value of '50' for the parameter which specifies what to change
    QString command = QString("\033]50;%1\a").arg(text);

    sendInput(command);
}
// Konqueror integration
bool Part::openUrl(const QUrl& aQUrl)
{
    QUrl aUrl = aQUrl;

    if (url() == aUrl) {
        emit completed();
        return true;
    }

    setUrl(aUrl);
    emit setWindowCaption(aUrl.toDisplayString(QUrl::PreferLocalFile));
    ////qDebug() << "Set Window Caption to " << url.pathOrUrl();
    emit started(0);

    if (aUrl.isLocalFile()) {
        showShellInDir(aUrl.path());
    } else {
        showShellInDir(QDir::homePath());
    }

    emit completed();
    return true;
}

void Part::setMonitorSilenceEnabled(bool enabled)
{
    Q_ASSERT(activeSession());

    if (enabled) {
        activeSession()->setMonitorSilence(true);
        connect(activeSession(), &Konsole::Session::stateChanged, this, &Konsole::Part::sessionStateChanged, Qt::UniqueConnection);
    } else {
        activeSession()->setMonitorSilence(false);
        disconnect(activeSession(), &Konsole::Session::stateChanged, this, &Konsole::Part::sessionStateChanged);
    }
}

void Part::setMonitorActivityEnabled(bool enabled)
{
    Q_ASSERT(activeSession());

    if (enabled) {
        activeSession()->setMonitorActivity(true);
        connect(activeSession(), &Konsole::Session::stateChanged, this, &Konsole::Part::sessionStateChanged, Qt::UniqueConnection);
    } else {
        activeSession()->setMonitorActivity(false);
        disconnect(activeSession(), &Konsole::Session::stateChanged, this, &Konsole::Part::sessionStateChanged);
    }
}

void Part::sessionStateChanged(int state)
{
    if (state == NOTIFYSILENCE)
        emit silenceDetected();
    else if (state == NOTIFYACTIVITY)
        emit activityDetected();
}

#include "Part.moc"
