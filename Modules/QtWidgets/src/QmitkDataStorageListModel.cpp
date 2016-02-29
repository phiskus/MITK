/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

#include "QmitkDataStorageListModel.h"

//# Own includes
// mitk
#include "mitkStringProperty.h"

//# Toolkit includes
// itk
#include "itkCommand.h"

QmitkDataStorageListModel::QmitkDataStorageListModel(mitk::DataStorage* dataStorage,
                                                               mitk::NodePredicateBase::Pointer pred,
                                                               QObject* parent)
:QAbstractListModel(parent),
 m_NodePredicate(nullptr),
 m_DataStorage(nullptr),
 m_BlockEvents(false)
{
  this->SetPredicate(pred);
  this->SetDataStorage(dataStorage);
}

QmitkDataStorageListModel::~QmitkDataStorageListModel()
{
  // set data storage to nullptr so that the event listener gets removed
  this->SetDataStorage(nullptr);
}

void QmitkDataStorageListModel::SetDataStorage(mitk::DataStorage::Pointer dataStorage)
{
  if( m_DataStorage == dataStorage)
  {
    return;
  }

  // remove old listeners
  if(m_DataStorage != nullptr)
  {
    m_DataStorage->AddNodeEvent.RemoveListener(
      mitk::MessageDelegate1<QmitkDataStorageListModel, const mitk::DataNode*>(
        this, &QmitkDataStorageListModel::OnDataStorageNodeAdded));

    m_DataStorage->RemoveNodeEvent.RemoveListener(
      mitk::MessageDelegate1<QmitkDataStorageListModel, const mitk::DataNode*>(
        this, &QmitkDataStorageListModel::OnDataStorageNodeRemoved));

    m_DataStorage->RemoveObserver(m_DataStorageDeleteObserverTag);
    m_DataStorageDeleteObserverTag = 0;
  }

  m_DataStorage = dataStorage;

  if(m_DataStorage != nullptr)
  {
    // subscribe for node added/removed events
    m_DataStorage->AddNodeEvent.AddListener(
      mitk::MessageDelegate1<QmitkDataStorageListModel, const mitk::DataNode*>(
        this, &QmitkDataStorageListModel::OnDataStorageNodeAdded));

    m_DataStorage->RemoveNodeEvent.AddListener(
      mitk::MessageDelegate1<QmitkDataStorageListModel, const mitk::DataNode*>(
        this, &QmitkDataStorageListModel::OnDataStorageNodeRemoved));

    // add ITK delete listener on data storage
    itk::MemberCommand<QmitkDataStorageListModel>::Pointer deleteCommand =
    itk::MemberCommand<QmitkDataStorageListModel>::New();
    deleteCommand->SetCallbackFunction(this,
                                         &QmitkDataStorageListModel::OnDataStorageDeleted);
    m_DataStorageDeleteObserverTag = m_DataStorage->AddObserver(itk::DeleteEvent(), deleteCommand);
  }

  // reset/rebuild model
  reset();
}

Qt::ItemFlags QmitkDataStorageListModel::flags(const QModelIndex&) const
{
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant QmitkDataStorageListModel::data(const QModelIndex& index, int role) const
{
  if(role == Qt::DisplayRole && index.isValid())
  {
    const mitk::DataNode* node = m_NodesAndObserverTags.at(index.row()).first;
    return QVariant(QString::fromStdString(node->GetName()));
  }
  else
  {
    return QVariant();
  }
}

QVariant QmitkDataStorageListModel::headerData(int  /*section*/, Qt::Orientation  /*orientation*/, int  /*role*/) const
{
  return QVariant(tr("Nodes"));
}

int QmitkDataStorageListModel::rowCount(const QModelIndex&  /*parent*/) const
{
  return m_NodesAndObserverTags.size();
}

std::vector<mitk::DataNode*> QmitkDataStorageListModel::GetDataNodes() const
{
  auto size = m_NodesAndObserverTags.size();
  std::vector<mitk::DataNode*> result(size);
  for (int i = 0; i < size; ++i)
  {
    result[i] = m_NodesAndObserverTags[i].first;
  }
  return result;
}

mitk::DataStorage* QmitkDataStorageListModel::GetDataStorage() const
{
  return m_DataStorage;
}


void QmitkDataStorageListModel::SetPredicate(mitk::NodePredicateBase* pred)
{
  m_NodePredicate = pred;

  // in a prior implementation the call to beginResetModel() been after reset().
  // Should this actually be the better order of calls, please document!
  QAbstractListModel::beginResetModel();
  reset();
  QAbstractListModel::endResetModel();
}

mitk::NodePredicateBase* QmitkDataStorageListModel::GetPredicate() const
{
  return m_NodePredicate;
}

void QmitkDataStorageListModel::reset()
{
  if(m_DataStorage == nullptr)
  {
    return;
  }

  mitk::DataStorage::SetOfObjects::ConstPointer modelNodes;

  if ( m_NodePredicate )
    modelNodes = m_DataStorage->GetSubset(m_NodePredicate);
  else
    modelNodes = m_DataStorage->GetAll();

  ClearInternalNodeList();

  // add all filtered nodes to our list
  for (auto& node : *modelNodes)
  {
      AddNodeToInternalList(node);
  }
}

void QmitkDataStorageListModel::AddNodeToInternalList(mitk::DataNode* node)
{
  if (m_DataStorage != nullptr)
  {
    itk::MemberCommand<QmitkDataStorageListModel>::Pointer modifiedCommand;
    // add modified observer
    modifiedCommand = itk::MemberCommand<QmitkDataStorageListModel>::New();
    modifiedCommand->SetCallbackFunction(this, &QmitkDataStorageListModel::OnDataNodeModified);
    unsigned long observerTag =  m_DataStorage->AddObserver(itk::ModifiedEvent(), modifiedCommand);

    m_NodesAndObserverTags.push_back( std::make_pair(node, observerTag) );
  }
}

void QmitkDataStorageListModel::ClearInternalNodeList()
{
  for(auto& iter : m_NodesAndObserverTags)
  {
    iter.first->RemoveObserver(iter.second);
  }
  m_NodesAndObserverTags.clear();
}

void QmitkDataStorageListModel::RemoveNodeFromInternalList(mitk::DataNode* node)
{
  for(auto iter = m_NodesAndObserverTags.begin();
      iter != m_NodesAndObserverTags.end();
      ++iter)
  {
    if (iter->first == node)
    {
      node->RemoveObserver(iter->second);
      m_NodesAndObserverTags.erase(iter); // invalidate iter
      break;
    }
  }
}

void QmitkDataStorageListModel::OnDataStorageNodeAdded( const mitk::DataNode* node )
{
  // guarantee no recursions when a new node event is thrown
  if(!m_BlockEvents)
  {
    m_BlockEvents = true;

    // check if node should be added to the model
    bool addNode = true;
    if(m_NodePredicate && !m_NodePredicate->CheckNode(node))
      addNode = false;

    if(addNode)
    {
      int newIndex = m_NodesAndObserverTags.size();
      beginInsertRows(QModelIndex(), newIndex, newIndex);
      AddNodeToInternalList(const_cast<mitk::DataNode*>(node));
      endInsertRows();
    }

    m_BlockEvents = false;
  }
}

void QmitkDataStorageListModel::OnDataStorageNodeRemoved( const mitk::DataNode* node )
{
  // guarantee no recursions when a new node event is thrown
  if(!m_BlockEvents)
  {
    m_BlockEvents = true;

    int row = 0;
    for(auto iter = m_NodesAndObserverTags.begin();
        iter != m_NodesAndObserverTags.end();
        ++iter, ++row)
    {
      if (iter->first == node)
      {
        // node found, remove it
        beginRemoveRows(QModelIndex(), row, row);
        RemoveNodeFromInternalList(iter->first);
        endRemoveRows();
        break;
      }
    }
  }

  m_BlockEvents = false;
}

void QmitkDataStorageListModel::OnDataNodeModified( const itk::Object *caller, const itk::EventObject & /*event*/ )
{
  if(m_BlockEvents) return;

  const mitk::DataNode* modifiedNode = dynamic_cast<const mitk::DataNode*>(caller);
  if(modifiedNode)
  {
    QModelIndex changedIndex = getIndex(modifiedNode);
    if (changedIndex.isValid())
    {
      emit dataChanged(changedIndex, changedIndex);
    }
  }
}

void QmitkDataStorageListModel::OnDataStorageDeleted( const itk::Object *caller, const itk::EventObject & /*event*/ )
{
  if(m_BlockEvents) return;

  // set data storage to nullptr -> empty model
  this->SetDataStorage(nullptr);
}

mitk::DataNode::Pointer QmitkDataStorageListModel::getNode( const QModelIndex &index ) const
{
  if(index.isValid())
  {
    return m_NodesAndObserverTags.at(index.row()).first;
  }
  else
  {
    return nullptr;
  }
}

QModelIndex QmitkDataStorageListModel::getIndex(const mitk::DataNode* node) const
{
  int row = 0;
  for ( auto iter = m_NodesAndObserverTags.begin();
       iter != m_NodesAndObserverTags.end();
       ++iter, ++row )
  {
    if ( iter->first == node )
    {
      return index(row);
    }
  }
  return QModelIndex();
}
