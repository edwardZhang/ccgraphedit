
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "myqglwidget.h"
#include "fileutil.h"
#include "mysceneeditor.h"
#include "dialogimportccb.h"
#include "nodeitem.h"
#include "NodeDriver.h"
#include "widgetpoint.h"
#include "componentnode.h"
#include "componentsprite.h"
#include "componentlayer.h"
#include "componentlabel.h"
#include "componentparticlesystem.h"
#include "exporterproject.h"
#include "importerproject.h"
#include "deviceframe.h"
#include "widgetTexture.h"

#include "cocos2d.h"
#include "CCFileUtils.h"
#include "CCClassRegistry.h"
#include "CCStreamFile.h"
#include "CCStreamFormatted.h"

#include "CCBReader/CCBReader.h"
#include "CCBReader/CCNodeLoaderLibrary.h"

#include <QStandardItemModel>
#include <QMenuBar>
#include <QAbstractListModel>
#include <QLineEdit>
#include <QCheckBox>
#include <QMessageBox>
#include <QToolBar>
#include <QToolButton>
#include <QSignalMapper>
#include <QFileDialog>
#include <QComboBox>

USING_NS_CC;
USING_NS_CC_EXT;

namespace
{
    const uint32_t kNodeDriverPosition = fnv1_32("position");
};

IMPLEMENT_SINGLETON(MainWindow)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , mQGLWidget(nullptr)
    , mToolbar(nullptr)
    , mDeviceCombo(nullptr)
    , mSelectedNode(nullptr)
    , mCurrentDeviceFrame(nullptr)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

Ui::MainWindow* MainWindow::UI()
{
    return ui;
}

bool MainWindow::Init()
{
    setWindowTitle(QString("Cocos2d Graph Editor"));

    // create a toolbar for the working area
    // add it to the working layout
    mToolbar = new QToolBar;
    ui->working->layout()->addWidget(mToolbar);

    // register the components
    RegisterComponent(Node::kClassId, new ComponentNode, "Node");
    RegisterComponent(Sprite::kClassId, new ComponentSprite, "Sprite");
    RegisterComponent(Layer::kClassId, new ComponentLayer, "Layer");
    RegisterComponent(LabelTTF::kClassId, new ComponentLabelTTF, "LabelTTF");
    RegisterComponent(LabelBMFont::kClassId, new ComponentLabelBMF, "LabelBMF");

    // connect any signals and slots
    connect(MySceneEditor::instance(), SIGNAL(positionChanged(Node*, Point&)), this, SLOT(setNodePosition(Node*,Point&)));
    connect(MySceneEditor::instance(), SIGNAL(selectedNodeChanged(Node*)), this, SLOT(setSelectedNode(Node*)));
    connect(ui->hierarchy->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this, SLOT(selectNode()));

    // add our cocos2dx opengl widget to the splitter in the correct place
    mQGLWidget = new MyQGLWidget;
    mQGLWidget->setSizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
    mQGLWidget->show(); // this must come before adding to the graph since it initializes cocos2d.
    ui->working->layout()->addWidget(mQGLWidget);

    // Add a path for our test sprite
    FileUtils::sharedFileUtils()->addSearchPath("../../../../../cocos2d/template/multi-platform-cpp/proj.ios");
    FileUtils::sharedFileUtils()->addSearchPath("/Users/jgraham/dev_qtTest/resources/images/frames");

    // setup the basic scene and root node
    Node* scene = Director::sharedDirector()->getRunningScene();
    NodeItem* item = AddNode(scene, Node::kClassId);
    MySceneEditor::instance()->SetRootNode(item->GetNode());

    // setup device frame combo box
    mDeviceCombo = new QComboBox;
    connect(mDeviceCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(selectDeviceFrame(int)));
    mToolbar->addWidget(mDeviceCombo);

    AddDeviceFrame(new DeviceFrame("iPhone", "frame-iphone.png", 320, 480,  DeviceFrame::kPortrait));
    AddDeviceFrame(new DeviceFrame("iPad",   "frame-ipad.png",   768, 1024, DeviceFrame::kPortrait));
    AddDeviceFrame(new DeviceFrame("iPhone landscape left", "frame-iphone.png", 480, 320,  DeviceFrame::kLandscapeLeft));
    AddDeviceFrame(new DeviceFrame("iPad landscape left",   "frame-ipad.png",   1024,768,  DeviceFrame::kLandscapeLeft));
    selectDeviceFrame(0);

    QMenu* menu = new QMenu("File", this);
    menuBar()->addMenu(menu);

    QAction* save = new QAction("Save Project", this);
    connect(save, SIGNAL(triggered()), this, SLOT(saveProject()));
    menu->addAction(save);

    QAction* load = new QAction("Load Project", this);
    connect(load, SIGNAL(triggered()), this, SLOT(loadProject()));
    menu->addAction(load);

    QAction* dump = new QAction("Dump Scene Graph", this);
    connect(dump, SIGNAL(triggered()), this, SLOT(dumpScene()));
    menu->addAction(dump);

    return true;
}

void MainWindow::SetWorkingDirectory(const char* path)
{
    mWorkingDirectory = path;
}

void MainWindow::AddFiles(const char* root, const char* path, bool directory)
{
    if (directory)
        FileUtils::sharedFileUtils()->addSearchPath(path);
}

NodeItem* MainWindow::AddNode(Node* parent, uint32_t classId)
{
    if (!ui->hierarchy)
    {
        QMessageBox::information(nullptr, QString("Error"), QString("Scene tree widget not available"), QMessageBox::Ok);
        return nullptr;
    }

    IComponent* component = MainWindow::instance()->FindComponent(classId);
    if (!component)
    {
        QMessageBox::information(nullptr, QString("Error"), QString("Component cannot be found to populate node item"), QMessageBox::Ok);
        return nullptr;
    }

    // Find the parent so that we can append to it in the tree view
    QTreeWidgetItem* parentItem;
    tNodeToNodeItemMap::iterator it = mNodeToNodeItemMap.find(parent);
    if (it == mNodeToNodeItemMap.end())
    {
        parentItem = ui->hierarchy->invisibleRootItem();
    }
    else
    {
        NodeItem* item = (*it).second;
        parentItem = item->SceneItem();
    }

    NodeItem* nodeItem = new NodeItem;

    Node* node = component->Instantiate(classId);
    if (!node)
    {
        QMessageBox::information(nullptr, QString("Error"), QString("Failed to instantiate node"), QMessageBox::Ok);
        return nullptr;
    }

    if (parent)
        parent->addChild(node);

    component->Populate(nodeItem, ui->properties, node);

    QTreeWidgetItem* sceneItem = new QTreeWidgetItem;
    sceneItem->setText(0, nodeItem->Name());

    nodeItem->SetNode(node);
    nodeItem->SetSceneItem(sceneItem);

    parentItem->addChild(sceneItem);

    mNodeToNodeItemMap.insert(tNodeToNodeItemMap::value_type(node, nodeItem));

    return nodeItem;
}

void MainWindow::ClearScene()
{
    tNodeToNodeItemMap::iterator it(mNodeToNodeItemMap.begin()), itEnd(mNodeToNodeItemMap.end());
    for (; it != itEnd; ++it)
    {
        NodeItem* item = (*it).second;
        Node* node = item->GetNode();
        node->removeFromParent();
        delete item;
    }
    mNodeToNodeItemMap.clear();
    ui->hierarchy->clear();
}

void MainWindow::RegisterNodeDriver(uint32_t driverId, INodeDriver *driver)
{
    mClassToNodeDriverMap.insert(tClassToNodeDriverMap::value_type(driverId, driver));
}

void MainWindow::RegisterComponent(uint32_t classId, IComponent* component, const char* componentName)
{
    component->RegisterDrivers();

    mClassToComponentMap.insert(tClassToComponentMap::value_type(classId, component));

    QAction* action = new QAction(QString(componentName), mToolbar);
    mToolbar->addAction(action);
    action->setData(QVariant((int)classId));
    connect(action, SIGNAL(triggered()), this, SLOT(performToolbarAction()));
}

IComponent* MainWindow::FindComponent(uint32_t classId)
{
    tClassToComponentMap::iterator it = mClassToComponentMap.find(classId);
    return it == mClassToComponentMap.end() ? nullptr : (*it).second;
}

//
// Public Slots
//

void MainWindow::selectDeviceFrame(int comboIndex)
{
    QVariant var = mDeviceCombo->itemData(comboIndex);
    DeviceFrame* frame = (DeviceFrame*)var.toLongLong();
    SetCurrentDeviceFrame(frame);
}

void MainWindow::saveProject()
{
    QString path = QFileDialog::getSaveFileName(this, QString("Save Project"), QString(""), QString(""));
    if (path.length())
    {
        StreamFile stream(path.toUtf8(), Stream::kWrite);
        StreamFormatted sf(&stream);

        ExporterProject* exporter = new ExporterProject;
        exporter->ExportToStream(sf);
        stream.close();
    }
}

void MainWindow::loadProject()
{
    ClearScene();

    QString path = QFileDialog::getOpenFileName(this, QString("Open Project"), QString(""), QString(""));
    if (path.length())
    {
        StreamFile stream(path.toUtf8(), Stream::kRead);
        StreamFormatted sf(&stream);

        ImporterProject* importer = new ImporterProject;
        importer->ImportFromStream(sf);
        stream.close();
    }
}

void MainWindow::dumpScene()
{
    DumpSceneGraph(Director::sharedDirector()->getRunningScene());
}

void MainWindow::selectNode()
{
    Node* selectedNode = GetSelectedNodeInHierarchy();
    MySceneEditor::instance()->SetSelectedNode(selectedNode);
    SetPropertyViewForNode(selectedNode, mSelectedNode);
    mSelectedNode = selectedNode;
}

void MainWindow::setNodePosition(Node* node, Point& position)
{
    NodeItem* nodeItem = GetNodeItemFromNode(node);
    if (nodeItem)
    {
        INodeDriver* driver = nodeItem->FindDriverByHash(kNodeDriverPosition);
        if (driver)
        {
            QWidget* widget = driver->Widget();
            widgetPoint* wp = dynamic_cast<widgetPoint*>(widget);
            if (wp)
            {
                wp->SetValue(position, true);
            }
        }
    }
}

void MainWindow::pushWidget(QWidget* widget)
{
    QVariant var = widget->property("node");
    Node* node = (Node*)var.toLongLong();
    if (node)
    {
        NodeItem* nodeItem = GetNodeItemFromNode(node);
        if (nodeItem)
            nodeItem->Push(widget);
    }
}

void MainWindow::setSelectedNode(Node* node)
{
    if (node)
        SetSelectedNodeInHierarchy(node);
}

void MainWindow::performToolbarAction()
{
    QAction* action = dynamic_cast<QAction*>(sender());
    if (action)
    {
        QVariant v = action->data();
        uint32_t classId = v.toInt();

        Node* parent = GetSelectedNodeInHierarchy();
        if (!parent)
            parent = MySceneEditor::instance()->GetRootNode();

        AddNode(parent, classId);
    }
}

DeviceFrame* MainWindow::GetCurrentDeviceFrame() const
{
    return mCurrentDeviceFrame;
}

//
// Protected Methods
//

void MainWindow::AddDeviceFrame(DeviceFrame* frame)
{
    mDeviceFrames.push_back(frame);
    mDeviceCombo->addItem(QString(frame->Name()), QVariant(qlonglong(frame)));
    SetCurrentDeviceFrame(frame);
}

void MainWindow::SetCurrentDeviceFrame(DeviceFrame* frame)
{
    mCurrentDeviceFrame = frame;
    MySceneEditor::instance()->SetFrameNode(mCurrentDeviceFrame->Sprite());
}

NodeItem* MainWindow::GetNodeItemFromNode(Node* node)
{
    tNodeToNodeItemMap::iterator it = mNodeToNodeItemMap.find(node);
    if (it == mNodeToNodeItemMap.end())
    {
        QMessageBox::information(nullptr, QString("Error"), QString("Node item cannot be found in the map"), QMessageBox::Ok);
        return nullptr;
    }

    NodeItem* nodeItem = (*it).second;
    return nodeItem;
}

Node* MainWindow::GetSelectedNodeInHierarchy()
{
    QList<QTreeWidgetItem*> nodes = ui->hierarchy->selectedItems();
    if (nodes.empty())
        return nullptr;

    QTreeWidgetItem* widget = nodes.front();
    QVariant var = widget->data(0, Qt::UserRole);
    NodeItem* item = (NodeItem*)var.toLongLong();

    return item->GetNode();
}

void MainWindow::SetSelectedNodeInHierarchy(Node* node)
{
    tNodeToNodeItemMap::iterator it = mNodeToNodeItemMap.find(node);
    if (it == mNodeToNodeItemMap.end())
    {
        QMessageBox::information(nullptr, QString("Error"), QString("Node cannot be found in the map"), QMessageBox::Ok);
        return;
    }
    NodeItem* nodeItem = (*it).second;
    ui->hierarchy->setCurrentItem(nodeItem->SceneItem());
}

void MainWindow::SetPropertyViewForNode(Node* node, Node* oldNode)
{
    if (node == oldNode)
        return;

    if (ui->properties)
    {
        // first unmap the widgets for the old node since we cannot recycle them
        // and they will be destroyed by the owning widgets or tree view widget.
        if (oldNode)
        {
            tNodeToNodeItemMap::iterator it = mNodeToNodeItemMap.find(oldNode);
            if (it == mNodeToNodeItemMap.end())
            {
                QMessageBox::information(nullptr, QString("Error"), QString("Old node cannot be found in the map"), QMessageBox::Ok);
            }
            else
            {
                NodeItem* nodeItem = (*it).second;
                nodeItem->DestroyWidgets();
            }
        }

        // remove all children of the root node
        QTreeWidgetItem* root = ui->properties->invisibleRootItem();
        while (root->childCount())
            root->takeChild(0);

        // Don't allow editing of the nodes above/next to root
        if (!MySceneEditor::instance()->IsChildOfRoot(node))
            return;

        tNodeToNodeItemMap::iterator it = mNodeToNodeItemMap.find(node);
        if (it == mNodeToNodeItemMap.end())
        {
            QMessageBox::information(nullptr, QString("Error"), QString("Node cannot be found in the map"), QMessageBox::Ok);
            return;
        }

        NodeItem* nodeItem = (*it).second;
        nodeItem->CreateWidgets(ui->properties);
    }
}

void MainWindow::DumpSceneGraph(Node* node, int indent)
{
    char foo[1000];
    memset(foo, 32, sizeof(foo));
    foo[indent * 4] = 0;

    qDebug(">%s%p %.2f,%.2f", foo, node, node->getPosition().x, node->getPosition().y);

    Object* obj;
    CCARRAY_FOREACH(node->getChildren(), obj)
    {
        Node* child = (Node*)obj;
        DumpSceneGraph(child, indent+1);
    }
}
