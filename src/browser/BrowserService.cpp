/*
 *  Copyright (C) 2013 Francois Ferrand
 *  Copyright (C) 2022 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "BrowserService.h"
#include "BrowserAction.h"
#include "BrowserEntryConfig.h"
#include "BrowserEntrySaveDialog.h"
#include "BrowserHost.h"
#include "BrowserMessageBuilder.h"
#include "BrowserSettings.h"
#include "core/Tools.h"
#include "gui/MainWindow.h"
#include "gui/MessageBox.h"
#include "gui/osutils/OSUtils.h"
#ifdef Q_OS_MACOS
#include "gui/osutils/macutils/MacUtils.h"
#endif

#include <QCheckBox>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QLocalSocket>
#include <QProgressDialog>
#include <QUrl>

const QString BrowserService::KEEPASSXCBROWSER_NAME = QStringLiteral("KeePassXC-Browser Settings");
const QString BrowserService::KEEPASSXCBROWSER_OLD_NAME = QStringLiteral("keepassxc-browser Settings");
static const QString KEEPASSXCBROWSER_GROUP_NAME = QStringLiteral("KeePassXC-Browser Passwords");
static int KEEPASSXCBROWSER_DEFAULT_ICON = 1;
// These are for the settings and password conversion
static const QString KEEPASSHTTP_NAME = QStringLiteral("KeePassHttp Settings");
static const QString KEEPASSHTTP_GROUP_NAME = QStringLiteral("KeePassHttp Passwords");
// Extra entry related options saved in custom data
const QString BrowserService::OPTION_SKIP_AUTO_SUBMIT = QStringLiteral("BrowserSkipAutoSubmit");
const QString BrowserService::OPTION_HIDE_ENTRY = QStringLiteral("BrowserHideEntry");
const QString BrowserService::OPTION_ONLY_HTTP_AUTH = QStringLiteral("BrowserOnlyHttpAuth");
const QString BrowserService::OPTION_NOT_HTTP_AUTH = QStringLiteral("BrowserNotHttpAuth");
const QString BrowserService::OPTION_OMIT_WWW = QStringLiteral("BrowserOmitWww");
// Multiple URL's
const QString BrowserService::ADDITIONAL_URL = QStringLiteral("KP2A_URL");

Q_GLOBAL_STATIC(BrowserService, s_browserService);

BrowserService::BrowserService()
    : QObject()
    , m_browserHost(new BrowserHost)
    , m_dialogActive(false)
    , m_bringToFrontRequested(false)
    , m_passwordGeneratorRequested(false)
    , m_prevWindowState(WindowState::Normal)
    , m_keepassBrowserUUID(Tools::hexToUuid("de887cc3036343b8974b5911b8816224"))
{
    connect(m_browserHost, &BrowserHost::clientMessageReceived, this, &BrowserService::processClientMessage);
    connect(getMainWindow(), &MainWindow::databaseUnlocked, this, &BrowserService::databaseUnlocked);
    connect(getMainWindow(), &MainWindow::databaseLocked, this, &BrowserService::databaseLocked);
    connect(getMainWindow(), &MainWindow::activeDatabaseChanged, this, &BrowserService::activeDatabaseChanged);

    setEnabled(browserSettings()->isEnabled());
}

BrowserService* BrowserService::instance()
{
    return s_browserService;
}

void BrowserService::setEnabled(bool enabled)
{
    if (enabled) {
        // Update KeePassXC/keepassxc-proxy binary paths to Native Messaging scripts
        if (browserSettings()->updateBinaryPath()) {
            browserSettings()->updateBinaryPaths();
        }

        m_browserHost->start();
    } else {
        m_browserHost->stop();
    }
}

bool BrowserService::isDatabaseOpened() const
{
    if (m_currentDatabaseWidget) {
        return !m_currentDatabaseWidget->isLocked();
    }
    return false;
}

bool BrowserService::openDatabase(bool triggerUnlock)
{
    if (!browserSettings()->unlockDatabase()) {
        return false;
    }

    if (m_currentDatabaseWidget && !m_currentDatabaseWidget->isLocked()) {
        return true;
    }

    if (triggerUnlock) {
        m_bringToFrontRequested = true;
        updateWindowState();
        emit requestUnlock();
    }

    return false;
}

void BrowserService::lockDatabase()
{
    if (m_currentDatabaseWidget) {
        m_currentDatabaseWidget->lock();
    }
}

QString BrowserService::getDatabaseHash(bool legacy)
{
    if (legacy) {
        return QCryptographicHash::hash(
                   (browserService()->getDatabaseRootUuid() + browserService()->getDatabaseRecycleBinUuid()).toUtf8(),
                   QCryptographicHash::Sha256)
            .toHex();
    }
    return QCryptographicHash::hash(getDatabaseRootUuid().toUtf8(), QCryptographicHash::Sha256).toHex();
}

QString BrowserService::getDatabaseRootUuid()
{
    auto db = getDatabase();
    if (!db) {
        return {};
    }

    Group* rootGroup = db->rootGroup();
    if (!rootGroup) {
        return {};
    }

    return rootGroup->uuidToHex();
}

QString BrowserService::getDatabaseRecycleBinUuid()
{
    auto db = getDatabase();
    if (!db) {
        return {};
    }

    Group* recycleBin = db->metadata()->recycleBin();
    if (!recycleBin) {
        return {};
    }
    return recycleBin->uuidToHex();
}

QJsonArray BrowserService::getChildrenFromGroup(Group* group)
{
    QJsonArray groupList;

    if (!group) {
        return groupList;
    }

    for (const auto& c : group->children()) {
        if (c == group->database()->metadata()->recycleBin()) {
            continue;
        }

        QJsonObject jsonGroup;
        jsonGroup["name"] = c->name();
        jsonGroup["uuid"] = Tools::uuidToHex(c->uuid());
        jsonGroup["children"] = getChildrenFromGroup(c);
        groupList.push_back(jsonGroup);
    }
    return groupList;
}

QJsonObject BrowserService::getDatabaseGroups()
{
    auto db = getDatabase();
    if (!db) {
        return {};
    }

    Group* rootGroup = db->rootGroup();
    if (!rootGroup) {
        return {};
    }

    QJsonObject root;
    root["name"] = rootGroup->name();
    root["uuid"] = Tools::uuidToHex(rootGroup->uuid());
    root["children"] = getChildrenFromGroup(rootGroup);

    QJsonArray groups;
    groups.push_back(root);

    QJsonObject result;
    result["groups"] = groups;

    return result;
}

QJsonObject BrowserService::createNewGroup(const QString& groupName)
{
    auto db = getDatabase();
    if (!db) {
        return {};
    }

    Group* rootGroup = db->rootGroup();
    if (!rootGroup) {
        return {};
    }

    auto group = rootGroup->findGroupByPath(groupName);

    // Group already exists
    if (group) {
        QJsonObject result;
        result["name"] = group->name();
        result["uuid"] = Tools::uuidToHex(group->uuid());
        return result;
    }

    auto dialogResult = MessageBox::warning(m_currentDatabaseWidget,
                                            tr("KeePassXC: Create a new group"),
                                            tr("A request for creating a new group \"%1\" has been received.\n"
                                               "Do you want to create this group?\n")
                                                .arg(groupName),
                                            MessageBox::Yes | MessageBox::No);

    if (dialogResult != MessageBox::Yes) {
        return {};
    }

    QString name, uuid;
    Group* previousGroup = rootGroup;
    auto groups = groupName.split("/");

    // Returns the group name based on depth
    auto getGroupName = [&](int depth) {
        QString gName;
        for (int i = 0; i < depth + 1; ++i) {
            gName.append((i == 0 ? "" : "/") + groups[i]);
        }
        return gName;
    };

    // Create new group(s) always when the path is not found
    for (int i = 0; i < groups.length(); ++i) {
        QString gName = getGroupName(i);
        auto tempGroup = rootGroup->findGroupByPath(gName);
        if (!tempGroup) {
            Group* newGroup = new Group();
            newGroup->setName(groups[i]);
            newGroup->setUuid(QUuid::createUuid());
            newGroup->setParent(previousGroup);
            name = newGroup->name();
            uuid = Tools::uuidToHex(newGroup->uuid());
            previousGroup = newGroup;
            continue;
        }

        previousGroup = tempGroup;
    }

    QJsonObject result;
    result["name"] = name;
    result["uuid"] = uuid;
    return result;
}

QString BrowserService::getCurrentTotp(const QString& uuid)
{
    QList<QSharedPointer<Database>> databases;
    if (browserSettings()->searchInAllDatabases()) {
        for (auto dbWidget : getMainWindow()->getOpenDatabases()) {
            auto db = dbWidget->database();
            if (db) {
                databases << db;
            }
        }
    } else {
        databases << getDatabase();
    }

    auto entryUuid = Tools::hexToUuid(uuid);
    for (const auto& db : databases) {
        auto entry = db->rootGroup()->findEntryByUuid(entryUuid, true);
        if (entry) {
            return entry->totp();
        }
    }

    return {};
}

QJsonArray BrowserService::findMatchingEntries(const QString& dbid,
                                               const QString& siteUrl,
                                               const QString& formUrl,
                                               const QString& realm,
                                               const StringPairList& keyList,
                                               const bool httpAuth)
{
    Q_UNUSED(dbid);
    const bool alwaysAllowAccess = browserSettings()->alwaysAllowAccess();
    const bool ignoreHttpAuth = browserSettings()->httpAuthPermission();
    const QString siteHost = QUrl(siteUrl).host();
    const QString formHost = QUrl(formUrl).host();

    // Check entries for authorization
    QList<Entry*> pwEntriesToConfirm;
    QList<Entry*> pwEntries;
    for (auto* entry : searchEntries(siteUrl, formUrl, keyList)) {
        auto entryCustomData = entry->customData();

        if (!httpAuth
            && ((entryCustomData->contains(BrowserService::OPTION_ONLY_HTTP_AUTH)
                 && entryCustomData->value(BrowserService::OPTION_ONLY_HTTP_AUTH) == TRUE_STR)
                || entry->group()->resolveCustomDataTriState(BrowserService::OPTION_ONLY_HTTP_AUTH) == Group::Enable)) {
            continue;
        }

        if (httpAuth
            && ((entryCustomData->contains(BrowserService::OPTION_NOT_HTTP_AUTH)
                 && entryCustomData->value(BrowserService::OPTION_NOT_HTTP_AUTH) == TRUE_STR)
                || entry->group()->resolveCustomDataTriState(BrowserService::OPTION_NOT_HTTP_AUTH) == Group::Enable)) {
            continue;
        }

        // HTTP Basic Auth always needs a confirmation
        if (!ignoreHttpAuth && httpAuth) {
            pwEntriesToConfirm.append(entry);
            continue;
        }

        switch (checkAccess(entry, siteHost, formHost, realm)) {
        case Denied:
            continue;

        case Unknown:
            if (alwaysAllowAccess) {
                pwEntries.append(entry);
            } else {
                pwEntriesToConfirm.append(entry);
            }
            break;

        case Allowed:
            pwEntries.append(entry);
            break;
        }
    }

    // Confirm entries
    QList<Entry*> selectedEntriesToConfirm =
        confirmEntries(pwEntriesToConfirm, siteUrl, siteHost, formHost, realm, httpAuth);
    if (!selectedEntriesToConfirm.isEmpty()) {
        pwEntries.append(selectedEntriesToConfirm);
    }

    if (pwEntries.isEmpty()) {
        return {};
    }

    // Ensure that database is not locked when the popup was visible
    if (!isDatabaseOpened()) {
        return {};
    }

    // Sort results
    pwEntries = sortEntries(pwEntries, siteUrl, formUrl);

    // Fill the list
    QJsonArray result;
    for (auto* entry : pwEntries) {
        result.append(prepareEntry(entry));
    }

    return result;
}

QList<Entry*> BrowserService::confirmEntries(QList<Entry*>& pwEntriesToConfirm,
                                             const QString& siteUrl,
                                             const QString& siteHost,
                                             const QString& formUrl,
                                             const QString& realm,
                                             const bool httpAuth)
{
    if (pwEntriesToConfirm.isEmpty() || m_dialogActive) {
        return {};
    }

    m_dialogActive = true;
    updateWindowState();
    BrowserAccessControlDialog accessControlDialog(m_currentDatabaseWidget);

    connect(m_currentDatabaseWidget, SIGNAL(databaseLockRequested()), &accessControlDialog, SLOT(reject()));

    connect(&accessControlDialog, &BrowserAccessControlDialog::disableAccess, [&](QTableWidgetItem* item) {
        auto entry = pwEntriesToConfirm[item->row()];
        denyEntry(entry, siteHost, formUrl, realm);
    });

    accessControlDialog.setItems(pwEntriesToConfirm, siteUrl, httpAuth);

    QList<Entry*> allowedEntries;
    auto ret = accessControlDialog.exec();
    if (ret == QDialog::Accepted) {
        for (auto item : accessControlDialog.getSelectedEntries()) {
            auto entry = pwEntriesToConfirm[item->row()];
            if (accessControlDialog.remember()) {
                allowEntry(entry, siteHost, formUrl, realm);
            }
            allowedEntries.append(entry);
        }
    }

    // Re-hide the application if it wasn't visible before
    hideWindow();
    m_dialogActive = false;

    return allowedEntries;
}

void BrowserService::showPasswordGenerator(QLocalSocket* socket,
                                           const QString& incrementedNonce,
                                           const QString& publicKey,
                                           const QString& secretKey)
{
    if (!m_passwordGenerator) {
        m_passwordGenerator.reset(PasswordGeneratorWidget::popupGenerator(m_currentDatabaseWidget));

        connect(m_passwordGenerator.data(), &PasswordGeneratorWidget::closed, m_passwordGenerator.data(), [=] {
            if (!m_passwordGenerator->isPasswordGenerated()) {
                auto errorMessage = browserMessageBuilder()->getErrorReply("generate-password",
                                                                           ERROR_KEEPASS_ACTION_CANCELLED_OR_DENIED);
                m_browserHost->sendClientMessage(socket, errorMessage);
            }

            m_passwordGenerator.reset();
            hideWindow();
            m_passwordGeneratorRequested = false;
        });

        connect(m_passwordGenerator.data(),
                &PasswordGeneratorWidget::appliedPassword,
                m_passwordGenerator.data(),
                [=](const QString& password) {
                    QJsonObject message = browserMessageBuilder()->buildMessage(incrementedNonce);
                    message["password"] = password;
                    m_browserHost->sendClientMessage(
                        socket,
                        browserMessageBuilder()->buildResponse(
                            "generate-password", message, incrementedNonce, publicKey, secretKey));
                    hideWindow();
                });
    }

    m_passwordGeneratorRequested = true;
    raiseWindow();
    m_passwordGenerator->raise();
    m_passwordGenerator->activateWindow();
}

bool BrowserService::isPasswordGeneratorRequested() const
{
    return m_passwordGeneratorRequested;
}

// Returns true if URLs are identical. Paths with "/" are removed during comparison.
// URLs without scheme reverts to https.
// Special handling is needed because QUrl::matches() with QUrl::StripTrailingSlash does not strip "/" paths.
bool BrowserService::isUrlIdentical(const QString& first, const QString& second) const
{
    auto trimUrl = [](QString url) {
        url = url.trimmed();
        if (url.endsWith("/")) {
            url.remove(url.length() - 1, 1);
        }

        return url;
    };

    if (first.isEmpty() || second.isEmpty()) {
        return false;
    }

    const auto firstUrl = trimUrl(first);
    const auto secondUrl = trimUrl(second);
    if (firstUrl == secondUrl) {
        return true;
    }

    return QUrl(firstUrl).matches(QUrl(secondUrl), QUrl::StripTrailingSlash);
}

QString BrowserService::storeKey(const QString& key)
{
    auto db = getDatabase();
    if (!db) {
        return {};
    }

    bool contains;
    auto dialogResult = MessageBox::Cancel;
    QString id;

    do {
        QInputDialog keyDialog(m_currentDatabaseWidget);
        connect(m_currentDatabaseWidget, SIGNAL(databaseLockRequested()), &keyDialog, SLOT(reject()));
        keyDialog.setWindowTitle(tr("KeePassXC: New key association request"));
        keyDialog.setLabelText(tr("You have received an association request for the following database:\n%1\n\n"
                                  "Give the connection a unique name or ID, for example:\nchrome-laptop.")
                                   .arg(db->metadata()->name().toHtmlEscaped()));
        keyDialog.setOkButtonText(tr("Save and allow access"));
        keyDialog.setWindowFlags(keyDialog.windowFlags() | Qt::WindowStaysOnTopHint);
        raiseWindow();
        keyDialog.show();
        keyDialog.activateWindow();
        keyDialog.raise();
        auto ok = keyDialog.exec();

        id = keyDialog.textValue();

        if (ok != QDialog::Accepted || id.isEmpty() || !isDatabaseOpened()) {
            hideWindow();
            return {};
        }

        contains = db->metadata()->customData()->contains(CustomData::BrowserKeyPrefix + id);
        if (contains) {
            dialogResult = MessageBox::warning(m_currentDatabaseWidget,
                                               tr("KeePassXC: Overwrite existing key?"),
                                               tr("A shared encryption key with the name \"%1\" "
                                                  "already exists.\nDo you want to overwrite it?")
                                                   .arg(id),
                                               MessageBox::Overwrite | MessageBox::Cancel,
                                               MessageBox::Cancel);
        }
    } while (contains && dialogResult == MessageBox::Cancel);

    hideWindow();
    db->metadata()->customData()->set(CustomData::BrowserKeyPrefix + id, key);
    db->metadata()->customData()->set(QString("%1_%2").arg(CustomData::Created, id),
                                      Clock::currentDateTime().toString(Qt::SystemLocaleShortDate));
    return id;
}

QString BrowserService::getKey(const QString& id)
{
    auto db = getDatabase();
    if (!db) {
        return {};
    }

    return db->metadata()->customData()->value(CustomData::BrowserKeyPrefix + id);
}

void BrowserService::addEntry(const QString& dbid,
                              const QString& login,
                              const QString& password,
                              const QString& siteUrl,
                              const QString& formUrl,
                              const QString& realm,
                              const QString& group,
                              const QString& groupUuid,
                              const bool downloadFavicon,
                              const QSharedPointer<Database>& selectedDb)
{
    // TODO: select database based on this key id
    Q_UNUSED(dbid);
    auto db = selectedDb ? selectedDb : selectedDatabase();
    if (!db) {
        return;
    }

    auto* entry = new Entry();
    entry->setUuid(QUuid::createUuid());
    entry->setTitle(QUrl(siteUrl).host());
    entry->setUrl(siteUrl);
    entry->setIcon(KEEPASSXCBROWSER_DEFAULT_ICON);
    entry->setUsername(login);
    entry->setPassword(password);

    // Select a group for the entry
    if (!group.isEmpty()) {
        if (db->rootGroup()) {
            auto selectedGroup = db->rootGroup()->findGroupByUuid(Tools::hexToUuid(groupUuid));
            if (selectedGroup) {
                entry->setGroup(selectedGroup);
            } else {
                entry->setGroup(getDefaultEntryGroup(db));
            }
        }
    } else {
        entry->setGroup(getDefaultEntryGroup(db));
    }

    const QString host = QUrl(siteUrl).host();
    const QString submitHost = QUrl(formUrl).host();
    BrowserEntryConfig config;
    config.allow(host);

    if (!submitHost.isEmpty()) {
        config.allow(submitHost);
    }
    if (!realm.isEmpty()) {
        config.setRealm(realm);
    }
    config.save(entry);

    if (downloadFavicon && m_currentDatabaseWidget) {
        m_currentDatabaseWidget->downloadFaviconInBackground(entry);
    }
}

bool BrowserService::updateEntry(const QString& dbid,
                                 const QString& uuid,
                                 const QString& login,
                                 const QString& password,
                                 const QString& siteUrl,
                                 const QString& formUrl)
{
    // TODO: select database based on this key id
    Q_UNUSED(dbid);
    auto db = selectedDatabase();
    if (!db) {
        return false;
    }

    Entry* entry = db->rootGroup()->findEntryByUuid(Tools::hexToUuid(uuid));
    if (!entry) {
        // If entry is not found for update, add a new one to the selected database
        addEntry(dbid, login, password, siteUrl, formUrl, "", "", "", db);
        return true;
    }

    // Check if the entry password is a reference. If so, update the original entry instead
    while (entry->attributes()->isReference(EntryAttributes::PasswordKey)) {
        const QUuid referenceUuid = entry->attributes()->referenceUuid(EntryAttributes::PasswordKey);
        if (!referenceUuid.isNull()) {
            entry = db->rootGroup()->findEntryByUuid(referenceUuid);
            if (!entry) {
                return false;
            }
        }
    }

    QString username = entry->username();
    if (username.isEmpty()) {
        return false;
    }

    bool result = false;
    if (username.compare(login, Qt::CaseSensitive) != 0
        || entry->password().compare(password, Qt::CaseSensitive) != 0) {
        MessageBox::Button dialogResult = MessageBox::No;
        if (!browserSettings()->alwaysAllowUpdate()) {
            raiseWindow();
            dialogResult = MessageBox::question(
                m_currentDatabaseWidget,
                tr("KeePassXC: Update Entry"),
                tr("Do you want to update the information in %1 - %2?").arg(QUrl(siteUrl).host(), username),
                MessageBox::Save | MessageBox::Cancel,
                MessageBox::Cancel,
                MessageBox::Raise);
        }

        if (browserSettings()->alwaysAllowUpdate() || dialogResult == MessageBox::Save) {
            entry->beginUpdate();
            if (!entry->attributes()->isReference(EntryAttributes::UserNameKey)) {
                entry->setUsername(login);
            }
            entry->setPassword(password);
            entry->endUpdate();
            result = true;
        }

        hideWindow();
    }

    return result;
}

bool BrowserService::deleteEntry(const QString& uuid)
{
    auto db = selectedDatabase();
    if (!db) {
        return false;
    }

    auto* entry = db->rootGroup()->findEntryByUuid(Tools::hexToUuid(uuid));
    if (!entry) {
        return false;
    }

    auto dialogResult = MessageBox::warning(m_currentDatabaseWidget,
                                            tr("KeePassXC: Delete entry"),
                                            tr("A request for deleting entry \"%1\" has been received.\n"
                                               "Do you want to delete the entry?\n")
                                                .arg(entry->title()),
                                            MessageBox::Yes | MessageBox::No);
    if (dialogResult != MessageBox::Yes) {
        return false;
    }

    db->recycleEntry(entry);
    return true;
}

QList<Entry*>
BrowserService::searchEntries(const QSharedPointer<Database>& db, const QString& siteUrl, const QString& formUrl)
{
    QList<Entry*> entries;
    auto* rootGroup = db->rootGroup();
    if (!rootGroup) {
        return entries;
    }

    for (const auto& group : rootGroup->groupsRecursive(true)) {
        if (group->isRecycled()
            || group->resolveCustomDataTriState(BrowserService::OPTION_HIDE_ENTRY) == Group::Enable) {
            continue;
        }

        const auto omitWwwSubdomain =
            group->resolveCustomDataTriState(BrowserService::OPTION_OMIT_WWW) == Group::Enable;

        for (auto* entry : group->entries()) {
            if (entry->isRecycled()
                || (entry->customData()->contains(BrowserService::OPTION_HIDE_ENTRY)
                    && entry->customData()->value(BrowserService::OPTION_HIDE_ENTRY) == TRUE_STR)) {
                continue;
            }

            if (!shouldIncludeEntry(entry, siteUrl, formUrl, omitWwwSubdomain)) {
                continue;
            }

            // Additional URL check may have already inserted the entry to the list
            if (!entries.contains(entry)) {
                entries.append(entry);
            }
        }
    }

    return entries;
}

QList<Entry*>
BrowserService::searchEntries(const QString& siteUrl, const QString& formUrl, const StringPairList& keyList)
{
    // Check if database is connected with KeePassXC-Browser
    auto databaseConnected = [&](const QSharedPointer<Database>& db) {
        for (const StringPair& keyPair : keyList) {
            QString key = db->metadata()->customData()->value(CustomData::BrowserKeyPrefix + keyPair.first);
            if (!key.isEmpty() && keyPair.second == key) {
                return true;
            }
        }
        return false;
    };

    // Get the list of databases to search
    QList<QSharedPointer<Database>> databases;
    if (browserSettings()->searchInAllDatabases()) {
        for (auto dbWidget : getMainWindow()->getOpenDatabases()) {
            auto db = dbWidget->database();
            if (db && databaseConnected(dbWidget->database())) {
                databases << db;
            }
        }
    } else {
        const auto& db = getDatabase();
        if (databaseConnected(db)) {
            databases << db;
        }
    }

    // Search entries matching the hostname
    QString hostname = QUrl(siteUrl).host();
    QList<Entry*> entries;
    do {
        for (const auto& db : databases) {
            entries << searchEntries(db, siteUrl, formUrl);
        }
    } while (entries.isEmpty() && removeFirstDomain(hostname));

    return entries;
}

void BrowserService::convertAttributesToCustomData(QSharedPointer<Database> db)
{
    if (!db) {
        return;
    }

    QList<Entry*> entries = db->rootGroup()->entriesRecursive();
    QProgressDialog progress(tr("Converting attributes to custom data…"), tr("Abort"), 0, entries.count());
    progress.setWindowModality(Qt::WindowModal);

    int counter = 0;
    int keyCounter = 0;
    for (auto* entry : entries) {
        if (progress.wasCanceled()) {
            return;
        }

        if (moveSettingsToCustomData(entry, KEEPASSHTTP_NAME)) {
            ++counter;
        }

        if (moveSettingsToCustomData(entry, KEEPASSXCBROWSER_OLD_NAME)) {
            ++counter;
        }

        if (moveSettingsToCustomData(entry, KEEPASSXCBROWSER_NAME)) {
            ++counter;
        }

        if (entry->title() == KEEPASSHTTP_NAME || entry->title().contains(KEEPASSXCBROWSER_NAME, Qt::CaseInsensitive)) {
            keyCounter += moveKeysToCustomData(entry, db);
            db->recycleEntry(entry);
        }

        progress.setValue(progress.value() + 1);
    }
    progress.reset();

    if (counter > 0) {
        MessageBox::information(nullptr,
                                tr("KeePassXC: Converted KeePassHTTP attributes"),
                                tr("Successfully converted attributes from %1 entry(s).\n"
                                   "Moved %2 keys to custom data.",
                                   "")
                                    .arg(counter)
                                    .arg(keyCounter),
                                MessageBox::Ok);
    } else if (counter == 0 && keyCounter > 0) {
        MessageBox::information(nullptr,
                                tr("KeePassXC: Converted KeePassHTTP attributes"),
                                tr("Successfully moved %n keys to custom data.", "", keyCounter),
                                MessageBox::Ok);
    } else {
        MessageBox::information(nullptr,
                                tr("KeePassXC: No entry with KeePassHTTP attributes found!"),
                                tr("The active database does not contain an entry with KeePassHTTP attributes."),
                                MessageBox::Ok);
    }

    // Rename password groupName
    Group* rootGroup = db->rootGroup();
    if (!rootGroup) {
        return;
    }

    for (auto* g : rootGroup->groupsRecursive(true)) {
        if (g->name() == KEEPASSHTTP_GROUP_NAME) {
            g->setName(KEEPASSXCBROWSER_GROUP_NAME);
            break;
        }
    }
}

void BrowserService::requestGlobalAutoType(const QString& search)
{
    emit osUtils->globalShortcutTriggered("autotype", search);
}

QList<Entry*> BrowserService::sortEntries(QList<Entry*>& pwEntries, const QString& siteUrl, const QString& formUrl)
{
    // Build map of prioritized entries
    QMultiMap<int, Entry*> priorities;
    for (auto* entry : pwEntries) {
        priorities.insert(sortPriority(getEntryURLs(entry), siteUrl, formUrl), entry);
    }

    auto keys = priorities.uniqueKeys();
    std::sort(keys.begin(), keys.end(), [](int l, int r) { return l > r; });

    QList<Entry*> results;
    for (auto key : keys) {
        results << priorities.values(key);

        if (browserSettings()->bestMatchOnly() && !results.isEmpty()) {
            // Early out once we find the highest batch of matches
            break;
        }
    }

    return results;
}

void BrowserService::allowEntry(Entry* entry, const QString& siteHost, const QString& formUrl, const QString& realm)
{
    BrowserEntryConfig config;
    config.load(entry);
    config.allow(siteHost);

    if (!formUrl.isEmpty() && siteHost != formUrl) {
        config.allow(formUrl);
    }

    if (!realm.isEmpty()) {
        config.setRealm(realm);
    }

    config.save(entry);
}

void BrowserService::denyEntry(Entry* entry, const QString& siteHost, const QString& formUrl, const QString& realm)
{
    BrowserEntryConfig config;
    config.load(entry);
    config.deny(siteHost);

    if (!formUrl.isEmpty() && siteHost != formUrl) {
        config.deny(formUrl);
    }

    if (!realm.isEmpty()) {
        config.setRealm(realm);
    }

    config.save(entry);
}

QJsonObject BrowserService::prepareEntry(const Entry* entry)
{
    QJsonObject res;
    res["login"] = entry->resolveMultiplePlaceholders(entry->username());
    res["password"] = entry->resolveMultiplePlaceholders(entry->password());
    res["name"] = entry->resolveMultiplePlaceholders(entry->title());
    res["uuid"] = entry->resolveMultiplePlaceholders(entry->uuidToHex());
    res["group"] = entry->resolveMultiplePlaceholders(entry->group()->name());

    if (entry->hasTotp()) {
        res["totp"] = entry->totp();
    }

    if (entry->isExpired()) {
        res["expired"] = TRUE_STR;
    }

    auto skipAutoSubmitGroup = entry->group()->resolveCustomDataTriState(BrowserService::OPTION_SKIP_AUTO_SUBMIT);
    if (skipAutoSubmitGroup == Group::Inherit) {
        if (entry->customData()->contains(BrowserService::OPTION_SKIP_AUTO_SUBMIT)) {
            res["skipAutoSubmit"] = entry->customData()->value(BrowserService::OPTION_SKIP_AUTO_SUBMIT);
        }
    } else {
        res["skipAutoSubmit"] = skipAutoSubmitGroup == Group::Enable ? TRUE_STR : FALSE_STR;
    }

    if (browserSettings()->supportKphFields()) {
        const EntryAttributes* attr = entry->attributes();
        QJsonArray stringFields;
        for (const auto& key : attr->keys()) {
            if (key.startsWith("KPH: ")) {
                QJsonObject sField;
                sField[key] = entry->resolveMultiplePlaceholders(attr->value(key));
                stringFields.append(sField);
            }
        }
        res["stringFields"] = stringFields;
    }
    return res;
}

BrowserService::Access
BrowserService::checkAccess(const Entry* entry, const QString& siteHost, const QString& formHost, const QString& realm)
{
    if (entry->isExpired() && !browserSettings()->allowExpiredCredentials()) {
        return Denied;
    }

    BrowserEntryConfig config;
    if (!config.load(entry)) {
        return Unknown;
    }
    if ((config.isAllowed(siteHost)) && (formHost.isEmpty() || config.isAllowed(formHost))) {
        return Allowed;
    }
    if ((config.isDenied(siteHost)) || (!formHost.isEmpty() && config.isDenied(formHost))) {
        return Denied;
    }
    if (!realm.isEmpty() && config.realm() != realm) {
        return Denied;
    }
    return Unknown;
}

Group* BrowserService::getDefaultEntryGroup(const QSharedPointer<Database>& selectedDb)
{
    auto db = selectedDb ? selectedDb : getDatabase();
    if (!db) {
        return nullptr;
    }

    auto* rootGroup = db->rootGroup();
    if (!rootGroup) {
        return nullptr;
    }

    for (auto* g : rootGroup->groupsRecursive(true)) {
        if (g->name() == KEEPASSXCBROWSER_GROUP_NAME && !g->isRecycled()) {
            return db->rootGroup()->findGroupByUuid(g->uuid());
        }
    }

    auto* group = new Group();
    group->setUuid(QUuid::createUuid());
    group->setName(KEEPASSXCBROWSER_GROUP_NAME);
    group->setIcon(KEEPASSXCBROWSER_DEFAULT_ICON);
    group->setParent(rootGroup);
    return group;
}

// Returns the maximum sort priority given a set of match urls and the
// extension provided site and form url.
int BrowserService::sortPriority(const QStringList& urls, const QString& siteUrl, const QString& formUrl)
{
    QList<int> priorityList;
    // NOTE: QUrl::matches is utterly broken in Qt < 5.11, so we work around that
    // by removing parts of the url that we don't match and direct matching others
    const auto stdOpts = QUrl::RemoveFragment | QUrl::RemoveUserInfo;
    const auto adjustedSiteUrl = QUrl(siteUrl).adjusted(stdOpts);
    const auto adjustedFormUrl = QUrl(formUrl).adjusted(stdOpts);

    auto getPriority = [&](const QString& givenUrl) {
        auto url = QUrl::fromUserInput(givenUrl).adjusted(stdOpts);

        // Default to https scheme if undefined
        if (url.scheme().isEmpty() || !givenUrl.contains("://")) {
            url.setScheme("https");
        }

        // Add the empty path to the URL if it's missing.
        // URL's from the extension always have a path set, entry URL's can be without.
        if (url.path().isEmpty() && !url.hasFragment() && !url.hasQuery()) {
            url.setPath("/");
        }

        // Reject invalid urls and hosts, except 'localhost', and scheme mismatch
        if (!url.isValid() || (!url.host().contains(".") && url.host() != "localhost")
            || url.scheme() != adjustedSiteUrl.scheme()) {
            return 0;
        }

        // Exact match with site url or form url
        if (url.matches(adjustedSiteUrl, QUrl::None) || url.matches(adjustedFormUrl, QUrl::None)) {
            return 100;
        }

        // Exact match without the query string
        if (url.matches(adjustedSiteUrl, QUrl::RemoveQuery) || url.matches(adjustedFormUrl, QUrl::RemoveQuery)) {
            return 90;
        }

        // Parent directory match
        if (url.isParentOf(adjustedSiteUrl) || url.isParentOf(adjustedFormUrl)) {
            return 85;
        }

        // Match without path (ie, FQDN match), form url prioritizes lower than site url
        if (url.host() == adjustedSiteUrl.host()) {
            return 80;
        }
        if (url.host() == adjustedFormUrl.host()) {
            return 70;
        }

        // Site/form url ends with given url (subdomain mismatch)
        if (adjustedSiteUrl.host().endsWith(url.host())) {
            return 60;
        }
        if (adjustedFormUrl.host().endsWith(url.host())) {
            return 50;
        }

        // No valid match found
        return 0;
    };

    for (const auto& entryUrl : urls) {
        priorityList << getPriority(entryUrl);
    }

    return *std::max_element(priorityList.begin(), priorityList.end());
}

bool BrowserService::schemeFound(const QString& url)
{
    QUrl address(url);
    return !address.scheme().isEmpty();
}

bool BrowserService::isIpAddress(const QString& host) const
{
    QHostAddress address(host);
    return address.protocol() == QAbstractSocket::IPv4Protocol || address.protocol() == QAbstractSocket::IPv6Protocol;
}

bool BrowserService::removeFirstDomain(QString& hostname)
{
    int pos = hostname.indexOf(".");
    if (pos < 0) {
        return false;
    }

    // Don't remove the second-level domain if it's the only one
    if (hostname.count(".") > 1) {
        hostname = hostname.mid(pos + 1);
        return !hostname.isEmpty();
    }

    // Nothing removed
    return false;
}

/* Test if a search URL matches a custom entry. If the URL has the schema "keepassxc", some special checks will be made.
 * Otherwise, this simply delegates to handleURL(). */
bool BrowserService::shouldIncludeEntry(Entry* entry,
                                        const QString& url,
                                        const QString& submitUrl,
                                        const bool omitWwwSubdomain)
{
    // Use this special scheme to find entries by UUID
    if (url.startsWith("keepassxc://by-uuid/")) {
        return url.endsWith("by-uuid/" + entry->uuidToHex());
    } else if (url.startsWith("keepassxc://by-path/")) {
        return url.endsWith("by-path/" + entry->path());
    }

    const auto allEntryUrls = entry->getAllUrls();
    for (const auto& entryUrl : allEntryUrls) {
        if (handleURL(entryUrl, url, submitUrl, omitWwwSubdomain)) {
            return true;
        }
    }

    return false;
}

bool BrowserService::handleURL(const QString& entryUrl,
                               const QString& siteUrl,
                               const QString& formUrl,
                               const bool omitWwwSubdomain)
{
    if (entryUrl.isEmpty()) {
        return false;
    }

    QUrl entryQUrl;
    if (entryUrl.contains("://")) {
        entryQUrl = entryUrl;
    } else {
        entryQUrl = QUrl::fromUserInput(entryUrl);

        if (browserSettings()->matchUrlScheme()) {
            entryQUrl.setScheme("https");
        }
    }

    // Remove WWW subdomain from matching if group setting is enabled
    if (omitWwwSubdomain && entryQUrl.host().startsWith("www.")) {
        entryQUrl.setHost(entryQUrl.host().remove("www."));
    }

    // Make a direct compare if a local file is used
    if (siteUrl.startsWith("file://")) {
        return entryUrl == formUrl;
    }

    // URL host validation fails
    if (entryQUrl.host().isEmpty()) {
        return false;
    }

    // Match port, if used
    QUrl siteQUrl(siteUrl);
    if (entryQUrl.port() > 0 && entryQUrl.port() != siteQUrl.port()) {
        return false;
    }

    // Match scheme
    if (browserSettings()->matchUrlScheme() && !entryQUrl.scheme().isEmpty()
        && entryQUrl.scheme().compare(siteQUrl.scheme()) != 0) {
        return false;
    }

    // Check for illegal characters
    QRegularExpression re("[<>\\^`{|}]");
    if (re.match(entryUrl).hasMatch()) {
        return false;
    }

    // Match the base domain
    if (getTopLevelDomainFromUrl(siteQUrl.host()) != getTopLevelDomainFromUrl(entryQUrl.host())) {
        return false;
    }

    // Match the subdomains with the limited wildcard
    if (siteQUrl.host().endsWith(entryQUrl.host())) {
        return true;
    }

    return false;
};

/**
 * Gets the base domain of URL.
 *
 * Returns the base domain, e.g. https://another.example.co.uk -> example.co.uk
 */
QString BrowserService::getTopLevelDomainFromUrl(const QString& url) const
{
    QUrl qurl = QUrl::fromUserInput(url);
    QString host = qurl.host();

    // If the hostname is an IP address, return it directly
    if (isIpAddress(host)) {
        return host;
    }

    if (host.isEmpty() || !host.contains(qurl.topLevelDomain())) {
        return {};
    }

    // Remove the top level domain part from the hostname, e.g. https://another.example.co.uk -> https://another.example
    host.chop(qurl.topLevelDomain().length());
    // Split the URL and select the last part, e.g. https://another.example -> example
    QString baseDomain = host.split('.').last();
    // Append the top level domain back to the URL, e.g. example -> example.co.uk
    baseDomain.append(qurl.topLevelDomain());
    return baseDomain;
}

QSharedPointer<Database> BrowserService::getDatabase()
{
    if (m_currentDatabaseWidget) {
        return m_currentDatabaseWidget->database();
    }
    return {};
}

QSharedPointer<Database> BrowserService::selectedDatabase()
{
    QList<DatabaseWidget*> databaseWidgets;
    for (auto dbWidget : getMainWindow()->getOpenDatabases()) {
        // Add only open databases
        if (!dbWidget->isLocked()) {
            databaseWidgets << dbWidget;
        }
    }

    BrowserEntrySaveDialog browserEntrySaveDialog(m_currentDatabaseWidget);
    int openDatabaseCount = browserEntrySaveDialog.setItems(databaseWidgets, m_currentDatabaseWidget);
    if (openDatabaseCount > 1) {
        int res = browserEntrySaveDialog.exec();
        if (res == QDialog::Accepted) {
            const auto selectedDatabase = browserEntrySaveDialog.getSelected();
            if (selectedDatabase.length() > 0) {
                int index = selectedDatabase[0]->data(Qt::UserRole).toInt();
                return databaseWidgets[index]->database();
            }
        } else {
            return {};
        }
    }

    // Return current database
    return getDatabase();
}

bool BrowserService::moveSettingsToCustomData(Entry* entry, const QString& name)
{
    if (entry->attributes()->contains(name)) {
        QString attr = entry->attributes()->value(name);
        entry->beginUpdate();
        if (!attr.isEmpty()) {
            entry->customData()->set(KEEPASSXCBROWSER_NAME, attr);
        }
        entry->attributes()->remove(name);
        entry->endUpdate();
        return true;
    }
    return false;
}

int BrowserService::moveKeysToCustomData(Entry* entry, QSharedPointer<Database> db)
{
    int keyCounter = 0;
    for (const auto& key : entry->attributes()->keys()) {
        if (key.contains(CustomData::BrowserLegacyKeyPrefix)) {
            QString publicKey = key;
            publicKey.remove(CustomData::BrowserLegacyKeyPrefix);

            // Add key to database custom data
            if (db && !db->metadata()->customData()->contains(CustomData::BrowserKeyPrefix + publicKey)) {
                db->metadata()->customData()->set(CustomData::BrowserKeyPrefix + publicKey,
                                                  entry->attributes()->value(key));
                ++keyCounter;
            }
        }
    }

    return keyCounter;
}

bool BrowserService::checkLegacySettings(QSharedPointer<Database> db)
{
    if (!db || !browserSettings()->isEnabled() || browserSettings()->noMigrationPrompt()) {
        return false;
    }

    bool legacySettingsFound = false;
    QList<Entry*> entries = db->rootGroup()->entriesRecursive();
    for (const auto& e : entries) {
        if (e->isRecycled()) {
            continue;
        }

        if ((e->attributes()->contains(KEEPASSHTTP_NAME) || e->attributes()->contains(KEEPASSXCBROWSER_NAME))
            || (e->title() == KEEPASSHTTP_NAME || e->title().contains(KEEPASSXCBROWSER_NAME, Qt::CaseInsensitive))) {
            legacySettingsFound = true;
            break;
        }
    }

    if (!legacySettingsFound) {
        return false;
    }

    auto* checkbox = new QCheckBox(tr("Don't show this warning again"));
    QObject::connect(checkbox, &QCheckBox::stateChanged, [&](int state) {
        browserSettings()->setNoMigrationPrompt(static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked);
    });

    auto dialogResult =
        MessageBox::warning(nullptr,
                            tr("KeePassXC: Legacy browser integration settings detected"),
                            tr("Your KeePassXC-Browser settings need to be moved into the database settings.\n"
                               "This is necessary to maintain your current browser connections.\n"
                               "Would you like to migrate your existing settings now?"),
                            MessageBox::Yes | MessageBox::No,
                            MessageBox::NoButton,
                            MessageBox::Raise,
                            checkbox);

    return dialogResult == MessageBox::Yes;
}

QStringList BrowserService::getEntryURLs(const Entry* entry)
{
    QStringList urlList;
    urlList << entry->url();

    // Handle additional URL's
    for (const auto& key : entry->attributes()->keys()) {
        if (key.startsWith(ADDITIONAL_URL)) {
            urlList << entry->attributes()->value(key);
        }
    }

    return urlList;
}

void BrowserService::hideWindow() const
{
    if (m_prevWindowState == WindowState::Minimized) {
        getMainWindow()->showMinimized();
    } else {
#ifdef Q_OS_MACOS
        if (m_prevWindowState == WindowState::Hidden) {
            macUtils()->hideOwnWindow();
        } else {
            macUtils()->raiseLastActiveWindow();
        }
#else
        if (m_prevWindowState == WindowState::Hidden) {
            getMainWindow()->hideWindow();
        } else {
            getMainWindow()->lower();
        }
#endif
    }
}

void BrowserService::raiseWindow(const bool force)
{
    m_prevWindowState = WindowState::Normal;
    if (getMainWindow()->isMinimized()) {
        m_prevWindowState = WindowState::Minimized;
    }
#ifdef Q_OS_MACOS
    Q_UNUSED(force)

    if (macUtils()->isHidden()) {
        m_prevWindowState = WindowState::Hidden;
    }
    macUtils()->raiseOwnWindow();
    Tools::wait(500);
#else
    if (getMainWindow()->isHidden()) {
        m_prevWindowState = WindowState::Hidden;
    }

    if (force) {
        getMainWindow()->bringToFront();
    }
#endif
}

void BrowserService::updateWindowState()
{
    m_prevWindowState = WindowState::Normal;
    if (getMainWindow()->isMinimized()) {
        m_prevWindowState = WindowState::Minimized;
    }
#ifdef Q_OS_MACOS
    if (macUtils()->isHidden()) {
        m_prevWindowState = WindowState::Hidden;
    }
#else
    if (getMainWindow()->isHidden()) {
        m_prevWindowState = WindowState::Hidden;
    }
#endif
}

void BrowserService::databaseLocked(DatabaseWidget* dbWidget)
{
    if (dbWidget) {
        QJsonObject msg;
        msg["action"] = QString("database-locked");
        m_browserHost->broadcastClientMessage(msg);
    }
}

void BrowserService::databaseUnlocked(DatabaseWidget* dbWidget)
{
    if (dbWidget) {
        if (m_bringToFrontRequested) {
            m_bringToFrontRequested = false;
            hideWindow();
        }

        QJsonObject msg;
        msg["action"] = QString("database-unlocked");
        m_browserHost->broadcastClientMessage(msg);

        auto db = dbWidget->database();
        if (checkLegacySettings(db)) {
            convertAttributesToCustomData(db);
        }
    }
}

void BrowserService::activeDatabaseChanged(DatabaseWidget* dbWidget)
{
    if (dbWidget) {
        if (dbWidget->isLocked()) {
            databaseLocked(dbWidget);
        } else {
            databaseUnlocked(dbWidget);
        }
    }

    m_currentDatabaseWidget = dbWidget;
}

void BrowserService::processClientMessage(QLocalSocket* socket, const QJsonObject& message)
{
    auto clientID = message["clientID"].toString();
    if (clientID.isEmpty()) {
        return;
    }

    // Create a new client action if we haven't seen this id yet
    if (!m_browserClients.contains(clientID)) {
        m_browserClients.insert(clientID, QSharedPointer<BrowserAction>::create());
    }

    auto& action = m_browserClients.value(clientID);
    auto response = action->processClientMessage(socket, message);
    m_browserHost->sendClientMessage(socket, response);
}
