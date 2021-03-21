#include "storedimagesdialog.h"

#include <entities/note.h>
#include <entities/notefolder.h>
#include <entities/notesubfolder.h>
#include <mainwindow.h>
#include <utils/gui.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QKeyEvent>
#include <QTreeWidgetItem>
#include <QtWidgets/QMessageBox>

#include "ui_storedimagesdialog.h"
#include "widgets/qownnotesmarkdowntextedit.h"

StoredImagesDialog::StoredImagesDialog(QWidget *parent)
    : MasterDialog(parent), ui(new Ui::StoredImagesDialog) {
    ui->setupUi(this);
    ui->fileTreeWidget->installEventFilter(this);

    QDir mediaDir(NoteFolder::currentMediaPath());

    if (!mediaDir.exists()) {
        ui->progressBar->setValue(ui->progressBar->maximum());
        return;
    }

    refreshMediaFiles();
}

StoredImagesDialog::~StoredImagesDialog() { delete ui; }

void StoredImagesDialog::refreshMediaFiles() {
    QDir mediaDir(NoteFolder::currentMediaPath());

    if (!mediaDir.exists()) {
        ui->progressBar->setValue(ui->progressBar->maximum());
        return;
    }

    QStringList mediaFiles = mediaDir.entryList(
        QStringList(QStringLiteral("*")), QDir::Files, QDir::Time);
    mediaFiles.removeDuplicates();

    QVector<Note> noteList = Note::fetchAll();
    int noteListCount = noteList.count();
    _fileNoteList.clear();

    ui->progressBar->setMaximum(noteListCount);
    ui->progressBar->show();

    Q_FOREACH (Note note, noteList) {
            QStringList mediaFileList = note.getMediaFileList();
            // we don't want to keep the whole note text
            note.setNoteText("");

            // remove all found images from the orphaned files list
            Q_FOREACH (QString fileName, mediaFileList) {
                    if (_orphanedImagesOnly) {
                        mediaFiles.removeAll(fileName);
                    }

                    _fileNoteList[fileName].append(note);
                }

            ui->progressBar->setValue(ui->progressBar->value() + 1);
        }

    ui->progressBar->hide();
    ui->fileTreeWidget->clear();

    Q_FOREACH (QString fileName, mediaFiles) {
            auto *item = new QTreeWidgetItem();
            item->setText(0, fileName);
            item->setData(0, Qt::UserRole, fileName);

            QString filePath = getFilePath(item);
            QFileInfo info(filePath);
            item->setToolTip(
                0, tr("Last modified at %1").arg(info.lastModified().toString()) +
                    QStringLiteral("\n") +
                    Utils::Misc::toHumanReadableByteSize(info.size()));

            ui->fileTreeWidget->addTopLevelItem(item);
        }

    // Re-apply search text
    on_searchLineEdit_textChanged(ui->searchLineEdit->text());

    // jump to the first item
    if (mediaFiles.count() > 0) {
        auto *event =
            new QKeyEvent(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QApplication::postEvent(ui->fileTreeWidget, event);
    }
}

/**
 * Shows the currently selected image
 *
 * @param current
 * @param previous
 */
void StoredImagesDialog::on_fileTreeWidget_currentItemChanged(
    QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    Q_UNUSED(previous);

    if (current == nullptr) {
        ui->notesFrame->hide();

        return;
    }

    auto *scene = new QGraphicsScene(this);
    QString filePath = getFilePath(current);

    if (!filePath.isEmpty()) {
        scene->addPixmap(QPixmap(filePath));
    }

    ui->graphicsView->setScene(scene);

    // Show which notes use the current file
    const QString fileName = current->text(0);
    if (_fileNoteList.contains(fileName)) {
        auto notes = _fileNoteList[fileName];
        ui->noteTreeWidget->clear();

        Q_FOREACH(Note note, notes) {
                QTreeWidgetItem *item = new QTreeWidgetItem();
                item->setText(0, note.getName());
                item->setData(0, Qt::UserRole, note.getId());

                NoteSubFolder noteSubFolder = note.getNoteSubFolder();
                if (noteSubFolder.isFetched()) {
                    item->setToolTip(0, tr("Path: %1").arg(
                        noteSubFolder.relativePath()));
                }

                ui->noteTreeWidget->addTopLevelItem(item);
            }

        ui->notesFrame->show();
    } else {
        ui->notesFrame->hide();
    }
}

/**
 * Gets the file path of a tree widget item
 *
 * @param item
 * @return
 */
QString StoredImagesDialog::getFilePath(QTreeWidgetItem *item) {
    if (item == Q_NULLPTR) {
        return QString();
    }

    QString fileName = NoteFolder::currentMediaPath() + QDir::separator() +
                       item->data(0, Qt::UserRole).toString();
    return fileName;
}

/**
 * Deletes selected images
 */
void StoredImagesDialog::on_deleteButton_clicked() {
    int selectedItemsCount = ui->fileTreeWidget->selectedItems().count();

    if (selectedItemsCount == 0) {
        return;
    }

    if (Utils::Gui::question(this, tr("Delete selected files"),
                             tr("Delete <strong>%n</strong> selected file(s)?",
                                "", selectedItemsCount),
                             QStringLiteral("delete-files")) !=
        QMessageBox::Yes) {
        return;
    }

    // delete all selected files
    Q_FOREACH (QTreeWidgetItem *item, ui->fileTreeWidget->selectedItems()) {
        QString filePath = getFilePath(item);
        bool removed = QFile::remove(filePath);

        if (removed) {
            delete item;
        }
    }
}

/**
 * Event filters
 *
 * @param obj
 * @param event
 * @return
 */
bool StoredImagesDialog::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (obj == ui->fileTreeWidget) {
            // delete the currently selected images
            if ((keyEvent->key() == Qt::Key_Delete) ||
                (keyEvent->key() == Qt::Key_Backspace)) {
                on_deleteButton_clicked();
                return true;
            }
            return false;
        }
    }

    return MasterDialog::eventFilter(obj, event);
}

void StoredImagesDialog::on_insertButton_clicked() {
#ifndef INTEGRATION_TESTS
    MainWindow *mainWindow = MainWindow::instance();
    if (mainWindow == Q_NULLPTR) {
        return;
    }
#else
    return;
#endif

    int selectedItemsCount = ui->fileTreeWidget->selectedItems().count();

    if (selectedItemsCount == 0) {
        return;
    }

    QOwnNotesMarkdownTextEdit *textEdit = mainWindow->activeNoteTextEdit();
    Note note = mainWindow->getCurrentNote();

    // insert all selected images
    Q_FOREACH (QTreeWidgetItem *item, ui->fileTreeWidget->selectedItems()) {
        QString filePath = getFilePath(item);
        QFileInfo fileInfo(filePath);
        QString mediaUrlString =
            note.mediaUrlStringForFileName(fileInfo.fileName());
        QString imageLink =
            "![" + fileInfo.baseName() + "](" + mediaUrlString + ")\n";
        textEdit->insertPlainText(imageLink);
        delete item;
    }
}

void StoredImagesDialog::on_checkBox_toggled(bool checked) {
    _orphanedImagesOnly = checked;
    refreshMediaFiles();
}

void StoredImagesDialog::on_searchLineEdit_textChanged(const QString &arg1) {
    Utils::Gui::searchForTextInTreeWidget(ui->fileTreeWidget, arg1,
      Utils::Gui::TreeWidgetSearchFlags(
        Utils::Gui::TreeWidgetSearchFlag::EveryWordSearch));
}
