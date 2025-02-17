/*
 *  Copyright (C) 2017 Weslly Honorato <weslly@protonmail.com>
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KEEPASSX_TOTPDIALOG_H
#define KEEPASSX_TOTPDIALOG_H

#include <QDialog>

#include "core/Database.h"
#include "gui/DatabaseWidget.h"

namespace Ui
{
    class TotpDialog;
}

class TotpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TotpDialog(QWidget* parent = nullptr, Entry* entry = nullptr);
    ~TotpDialog() override;

private Q_SLOTS:
    void updateTotp();
    void updateProgressBar();
    void updateSeconds();
    void copyToClipboard();

private:
    QScopedPointer<Ui::TotpDialog> m_ui;

    void resetCounter();
    Entry* m_entry;
    int m_counter;
    uint m_step;
    QTimer m_totpUpdateTimer;
};

#endif // KEEPASSX_TOTPDIALOG_H
