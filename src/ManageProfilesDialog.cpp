/*
    Copyright (C) 2007 by Robert Knight <robertknight@gmail.com>

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
#include "ManageProfilesDialog.h"

// Qt
#include <QtGui/QCheckBox>
#include <QtGui/QHeaderView>
#include <QtGui/QItemDelegate>
#include <QtGui/QItemEditorCreator>
#include <QtCore/QMetaEnum>
#include <QtGui/QScrollBar>
#include <QtGui/QStandardItem>

// Konsole
#include "EditProfileDialog.h"
#include "SessionManager.h"
#include "ui_ManageProfilesDialog.h"

using namespace Konsole;

ManageProfilesDialog::ManageProfilesDialog(QWidget* parent)
    : KDialog(parent)
{
    setCaption(i18n("Manage Profiles"));

    _ui = new Ui::ManageProfilesDialog();
    _ui->setupUi(mainWidget());

    // hide vertical header
    _ui->sessionTable->verticalHeader()->hide();
    _ui->sessionTable->setItemDelegateForColumn(1,new ProfileItemDelegate(this));

    // update table and listen for changes to the session types
    updateTableModel();
    connect( SessionManager::instance() , SIGNAL(profileAdded(const QString&)) , this,
             SLOT(updateTableModel()) );
    connect( SessionManager::instance() , SIGNAL(profileRemoved(const QString&)) , this,
             SLOT(updateTableModel()) );
    connect( SessionManager::instance() , SIGNAL(profileChanged(const QString&)) , this,
             SLOT(updateTableModel()) );

    // ensure that session names are fully visible
    _ui->sessionTable->resizeColumnToContents(0);
    _ui->sessionTable->resizeColumnToContents(1);

    // resize the session table to the full width of the table
    _ui->sessionTable->horizontalHeader()->setStretchLastSection(true);
    _ui->sessionTable->horizontalHeader()->setHighlightSections(false);
    
    // setup buttons
    connect( _ui->newSessionButton , SIGNAL(clicked()) , this , SLOT(newType()) );
    connect( _ui->editSessionButton , SIGNAL(clicked()) , this , SLOT(editSelected()) );
    connect( _ui->deleteSessionButton , SIGNAL(clicked()) , this , SLOT(deleteSelected()) );
    connect( _ui->setAsDefaultButton , SIGNAL(clicked()) , this , SLOT(setSelectedAsDefault()) );

}
ManageProfilesDialog::~ManageProfilesDialog()
{
    delete _ui;
}
void ManageProfilesDialog::itemDataChanged(QStandardItem* item)
{
   static const int ShortcutColumn = 2;

   if ( item->column() == ShortcutColumn )
   {
        QKeySequence sequence = QKeySequence::fromString(item->text());

        qDebug() << "New key sequence: " << item->text(); 

        SessionManager::instance()->setShortcut(item->data(Qt::UserRole+1).value<QString>(),
                                                sequence); 
   } 
}
void ManageProfilesDialog::updateTableModel()
{
    // ensure profiles list is complete
    // this may be EXPENSIVE, but will only be done the first time
    // that the dialog is shown. 
    SessionManager::instance()->loadAllProfiles();

    // setup session table
    _sessionModel = new QStandardItemModel(this);
    _sessionModel->setHorizontalHeaderLabels( QStringList() << i18n("Name")
                                                            << i18n("Show in Menu") 
                                                            << i18n("Shortcut") );
    QListIterator<QString> keyIter( SessionManager::instance()->availableProfiles() );
    while ( keyIter.hasNext() )
    {
        const QString& key = keyIter.next();

        Profile* info = SessionManager::instance()->profile(key);

        if ( info->isHidden() )
            continue;

        QList<QStandardItem*> itemList;
        QStandardItem* item = new QStandardItem( info->name() );

        if ( !info->icon().isEmpty() )
            item->setIcon( KIcon(info->icon()) );
        item->setData(key);

        const bool isFavorite = SessionManager::instance()->findFavorites().contains(key);

        // favorite column
        QStandardItem* favoriteItem = new QStandardItem();
        if ( isFavorite )
            favoriteItem->setData(KIcon("favorites"),Qt::DecorationRole);
        else
            favoriteItem->setData(KIcon(),Qt::DecorationRole);

        favoriteItem->setData(key,Qt::UserRole+1);

        // shortcut column
        QStandardItem* shortcutItem = new QStandardItem();
        QString shortcut = SessionManager::instance()->shortcut(key).
                                toString();
        shortcutItem->setText(shortcut);
        shortcutItem->setData(key,Qt::UserRole+1);

        itemList << item << favoriteItem << shortcutItem;

        _sessionModel->appendRow(itemList);
    }
    updateDefaultItem();

    connect( _sessionModel , SIGNAL(itemChanged(QStandardItem*)) , this , 
            SLOT(itemDataChanged(QStandardItem*)) );

    _ui->sessionTable->setModel(_sessionModel);

    // listen for changes in the table selection and update the state of the form's buttons
    // accordingly.
    //
    // it appears that the selection model is changed when the model itself is replaced,
    // so the signals need to be reconnected each time the model is updated.
    //
    // the view ( _ui->sessionTable ) has a selectionChanged() signal which it would be
    // preferable to connect to instead of the one belonging to the view's current 
    // selection model, but QAbstractItemView::selectionChanged() is protected
    //
    connect( _ui->sessionTable->selectionModel() , 
            SIGNAL(selectionChanged(const QItemSelection&,const QItemSelection&)) , this ,
            SLOT(tableSelectionChanged(const QItemSelection&)) );

    tableSelectionChanged( _ui->sessionTable->selectionModel()->selection() );

}
void ManageProfilesDialog::updateDefaultItem()
{
    const QString& defaultKey = SessionManager::instance()->defaultProfileKey();

    for ( int i = 0 ; i < _sessionModel->rowCount() ; i++ )
    {
        QStandardItem* item = _sessionModel->item(i);
        QFont font = item->font();

        bool isDefault = ( defaultKey == item->data().value<QString>() );

        if ( isDefault && !font.bold() )
        {
            font.setBold(true);
            item->setFont(font);
        } 
        else if ( !isDefault && font.bold() )
        {
            font.setBold(false);
            item->setFont(font);
        } 
    }
}
void ManageProfilesDialog::tableSelectionChanged(const QItemSelection& selection)
{
    bool enable = !selection.indexes().isEmpty();
    const SessionManager* manager = SessionManager::instance();
    const bool isNotDefault = enable && selectedKey() != manager->defaultProfileKey();

    _ui->editSessionButton->setEnabled(enable);
    // do not allow the default session type to be removed
    _ui->deleteSessionButton->setEnabled(isNotDefault);
    _ui->setAsDefaultButton->setEnabled(isNotDefault); 
}
void ManageProfilesDialog::deleteSelected()
{
    Q_ASSERT( selectedKey() != SessionManager::instance()->defaultProfileKey() );

    SessionManager::instance()->deleteProfile(selectedKey());
}
void ManageProfilesDialog::setSelectedAsDefault()
{
    SessionManager::instance()->setDefaultProfile(selectedKey());
    // do not allow the new default session type to be removed
    _ui->deleteSessionButton->setEnabled(false);
    _ui->setAsDefaultButton->setEnabled(false);

    // update font of new default item
    updateDefaultItem(); 
}
void ManageProfilesDialog::newType()
{
    EditProfileDialog dialog(this);
 
    // setup a temporary profile, inheriting from the default profile
    Profile* defaultProfile = SessionManager::instance()->defaultProfile();

    Profile* newProfile = new Profile(defaultProfile);
    newProfile->setProperty(Profile::Name,i18n("New Profile"));
    const QString& key = SessionManager::instance()->addProfile(newProfile);
    dialog.setProfile(key); 
    
    // if the user doesn't accept the dialog, remove the temporary profile
    // if they do accept the dialog, it will become a permanent profile
    if ( dialog.exec() != QDialog::Accepted )
        SessionManager::instance()->deleteProfile(key);
}
void ManageProfilesDialog::editSelected()
{
    EditProfileDialog dialog(this);
    dialog.setProfile(selectedKey());
    dialog.exec();
}
QString ManageProfilesDialog::selectedKey() const
{
    Q_ASSERT( _ui->sessionTable->selectionModel() &&
              _ui->sessionTable->selectionModel()->selectedRows().count() == 1 );
    // TODO There has to be an easier way of getting the data
    // associated with the currently selected item
    return _ui->sessionTable->
            selectionModel()->
            selectedIndexes().first().data( Qt::UserRole + 1 ).value<QString>();
}

ProfileItemDelegate::ProfileItemDelegate(QObject* parent)
    : QItemDelegate(parent)
{
}
bool ProfileItemDelegate::editorEvent(QEvent* event,QAbstractItemModel* model,
                                    const QStyleOptionViewItem&,const QModelIndex& index)
{
     if ( event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress )
     {
         const QString& key = index.data(Qt::UserRole + 1).value<QString>();
         const bool isFavorite = !SessionManager::instance()->findFavorites().contains(key);
                                                
        SessionManager::instance()->setFavorite(key,
                                            isFavorite);

         if ( isFavorite )
            model->setData(index,KIcon("favorites"),Qt::DecorationRole);
         else
            model->setData(index,KIcon(),Qt::DecorationRole);
     }
     
     return true; 
}

#include "ManageProfilesDialog.moc"
