#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "data/config.h"
#include "data/instance.h"

#include "util.h"

#include <memory>

#include <QDebug>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <QMenu>
#include <QShortcut>
#include <QClipboard>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>

#include <QDesktopServices>
#include <QUrl>

using MultiBound::MainWindow;
using MultiBound::Instance;

namespace {
    inline Instance* instanceFromItem(QListWidgetItem* itm) { return static_cast<Instance*>(itm->data(Qt::UserRole).value<void*>()); }
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // hide elements not in use yet
    ui->statusBar->setVisible(false);

    connect(ui->launchButton, &QPushButton::pressed, this, [this] { launch(); });
    connect(ui->instanceList, &QListWidget::doubleClicked, this, [this](const QModelIndex& ind) { if (ind.isValid()) launch(); });

    connect(ui->instanceList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        auto m = new QMenu(this);

        if (auto i = ui->instanceList->itemAt(pt); i) {
            auto inst = instanceFromItem(i);
            m->addAction(qs("Launch Instance"), this, [this, inst] { launch(inst); });
            if (auto id = inst->workshopId(); !id.isEmpty()) {
                m->addAction(qs("Update from Workshop collection"), [this, inst] { updateFromWorkshop(inst); });
                m->addAction(qs("Open Workshop link..."), [id] {
                    QDesktopServices::openUrl(QUrl(Util::workshopLinkFromId(id)));
                });
            }
            m->addSeparator();
        }

        { /* new menu */ } {
            auto mm = m->addMenu(qs("New instance"));
            mm->addAction("From Steam Workshop collection...", this, [this] {
                newFromWorkshop();
            });
        }
        m->addAction(qs("Refresh"), this, [this] { refresh(); });

        m->setAttribute(Qt::WA_DeleteOnClose);
        m->popup(ui->instanceList->mapToGlobal(pt));
    });

    connect(new QShortcut(QKeySequence(QKeySequence::Paste), this), &QShortcut::activated, this, [this] {
        auto txt = QApplication::clipboard()->text();
        auto id = Util::workshopIdFromLink(txt);
        if (id.isEmpty()) return;
        newFromWorkshop(id);
    });

    if (auto fi = QFileInfo(Config::starboundPath); !fi.isFile() || !fi.isExecutable()) {
        // prompt for working file path if executable missing
        auto d = fi.dir();
        if (!d.exists()) { // walk up until dir exists
            auto cd = qs("..");
            while (!d.cd(cd)) cd.append(qs("/.."));
        }

        auto fn = QFileDialog::getOpenFileName(this, qs("Locate Starbound executable..."), d.absolutePath());
        if (fn.isEmpty()) {
            QMetaObject::invokeMethod(this, [this] { close(); QApplication::quit(); }, Qt::ConnectionType::QueuedConnection);
        } else { // set new path and refresh
            Config::starboundPath = fn;
            Config::save();
            Config::load();
        }
    }

    refresh();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::refresh(const QString& focusPath) {
    QString selPath;
    if (!focusPath.isEmpty()) selPath = focusPath;
    else if (auto sel = ui->instanceList->selectedItems(); sel.count() > 0) selPath = instanceFromItem(sel[0])->path;

    ui->instanceList->clear();

    QListWidgetItem* selItm = nullptr;

    // load instances
    instances.clear();
    QDir d(Config::instanceRoot);
    auto lst = d.entryList(QDir::Dirs);
    instances.reserve(static_cast<size_t>(lst.count()));
    for (auto idir : lst) {
        auto inst = Instance::loadFrom(idir);
        if (!inst) continue;
        instances.push_back(inst);
        auto itm = new QListWidgetItem(inst->displayName(), ui->instanceList);
        itm->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(inst.get())));//reinterpret_cast<qintptr>(inst.get()));
        if (inst->path == selPath) selItm = itm;
    }

    if (selItm) {
        ui->instanceList->setCurrentRow(ui->instanceList->row(selItm));
        ui->instanceList->scrollToItem(selItm);
    }

}

void MainWindow::launch(Instance* inst) {
    if (!inst) { inst = selectedInstance(); if (!inst) return; }
    hide();
    inst->launch();
    show();
}

void MainWindow::updateFromWorkshop(Instance* inst) {
    if (!inst) { inst = selectedInstance(); if (!inst) return; }
    setEnabled(false);
    Util::updateFromWorkshop(inst);
    setEnabled(true);
    refresh(inst->path);
}

void MainWindow::newFromWorkshop(const QString& id_) {
    auto id = id_;
    if (id.isEmpty()) { // prompt
        bool ok = false;
        auto link = QInputDialog::getText(this, qs("Enter collection link"), qs("Enter a link to a Steam Workshop collection:"), QLineEdit::Normal, qs(), &ok);
        id = Util::workshopIdFromLink(link);
        if (!ok || id.isEmpty()) return;
    }

    setEnabled(false);
    auto inst = findWorkshopId(id);
    if (inst) {
        return updateFromWorkshop(inst);
    } else {
        auto ni = std::make_shared<Instance>();
        ni->json = QJsonDocument::fromJson(qs("{\"info\" : { \"workshopId\" : \"%1\" }, \"savePath\" : \"inst:/storage/\", \"assetSources\" : [ \"inst:/mods/\" ] }").arg(id).toUtf8()).object();
        Util::updateFromWorkshop(ni.get(), false);
        if (!ni->displayName().isEmpty()) {
            bool ok = false;
            auto name = QInputDialog::getText(this, qs("Directory name?"), qs("Enter a directory name for your new instance:"), QLineEdit::Normal, ni->displayName(), &ok);
            if (ok && !name.isEmpty()) {
                auto path = Util::splicePath(Config::instanceRoot, name);
                if (QDir(path).exists()) {
                    QMessageBox::warning(this, qs("Error creating instance"), qs("Directory already exists."));
                } else {
                    ni->path = path;
                    ni->save();
                    refresh(ni->path);
                }
            }
        }
    }
    setEnabled(true);
}

Instance* MainWindow::selectedInstance() {
    if (auto sel = ui->instanceList->selectedItems(); sel.count() > 0) return instanceFromItem(sel[0]);
    return nullptr;
}

Instance* MainWindow::findWorkshopId(const QString& id) {
    if (id.isEmpty()) return nullptr;
    for (auto& i : instances) if (i->workshopId() == id) return i.get();
    return nullptr;
}












//
