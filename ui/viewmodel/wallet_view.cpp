// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wallet_view.h"

#include <iomanip>
#include "ui_helpers.h"
#include <QApplication>
#include <QClipboard>
#include "model/app_model.h"
#include "qrcode/QRCodeGenerator.h"
#include <QtGui/qimage.h>
#include <QtCore/qbuffer.h>

using namespace beam;
using namespace std;
using namespace beamui;

namespace
{
    const int kDefaultFeeInGroth = 10;

    template<typename T>
    bool compareTx(const T& lf, const T& rt, Qt::SortOrder sortOrder)
    {
        if (sortOrder == Qt::DescendingOrder)
            return lf > rt;
        return lf < rt;
    }
}

TxObject::TxObject(const TxDescription& tx) : _tx(tx) 
{
    auto kernelID = QString::fromStdString(to_hex(_tx.m_kernelID.m_pData, _tx.m_kernelID.nBytes));
    setKernelID(kernelID);
}

bool TxObject::income() const
{
    return _tx.m_sender == false;
}

QString TxObject::date() const
{
    return toString(_tx.m_createTime);
}

QString TxObject::user() const
{
    return toString(_tx.m_peerId);
}

QString TxObject::userName() const
{
    return _userName;
}

QString TxObject::displayName() const
{
    return _displayName;
}

QString TxObject::comment() const
{
    string str{ _tx.m_message.begin(), _tx.m_message.end() };

    return QString(str.c_str()).trimmed();
}

QString TxObject::amount() const
{
    return BeamToString(_tx.m_amount);
}

QString TxObject::change() const
{
    if (_tx.m_change)
    {
        return BeamToString(_tx.m_change);
    }
    return QString{};
}

QString TxObject::status() const
{
    static QString Names[] = { tr("Pending"), tr("In Progress"), tr("Cancelled"), tr("Completed"), tr("Failed"), tr("Confirming") };
    return Names[static_cast<uint32_t>(_tx.m_status)];
}

bool TxObject::canCancel() const
{
    return _tx.canCancel();
}

bool TxObject::canDelete() const
{
    return _tx.canDelete();
}

void TxObject::setUserName(const QString& name)
{
    if (_userName != name)
    {
        _userName = name;
        emit displayNameChanged();
    }
}

void TxObject::setDisplayName(const QString& name)
{
    if (_displayName != name)
    {
        _displayName = name;
        emit displayNameChanged();
    }
}

beam::WalletID TxObject::peerId() const
{
    return _tx.m_peerId;
}

QString TxObject::getSendingAddress() const
{
    if (_tx.m_sender)
    {
        return toString(_tx.m_myId);
    }
    return user();
}

QString TxObject::getReceivingAddress() const
{
    if (_tx.m_sender)
    {
        return user();
    }
    return toString(_tx.m_myId);
}

QString TxObject::getFee() const
{
    if (_tx.m_fee)
    {
        return BeamToString(_tx.m_fee);
    }
    return QString{};
}

const beam::TxDescription& TxObject::getTxDescription() const
{
    return _tx;
}

void TxObject::setStatus(beam::TxStatus status)
{
    if (_tx.m_status != status)
    {
        _tx.m_status = status;
        emit statusChanged();
    }
}

QString TxObject::getKernelID() const
{
    return _kernelID;
}

void TxObject::setKernelID(const QString& value)
{
    if (_kernelID != value)
    {
        _kernelID = value;
        emit kernelIDChanged();
    }
}

void TxObject::update(const beam::TxDescription& tx)
{
    setStatus(tx.m_status);
    auto kernelID = QString::fromStdString(to_hex(tx.m_kernelID.m_pData, tx.m_kernelID.nBytes));
    setKernelID(kernelID);
}

WalletViewModel::WalletViewModel()
    : _model(*AppModel::getInstance()->getWallet())
    , _status{ 0, 0, 0, 0, {0, 0, 0}, {} }
    , _sendAmount("0")
    , _feeGrothes("0")
    , _change(0)
    , _expires(0)
{
    connect(&_model, SIGNAL(onStatus(const WalletStatus&)), SLOT(onStatus(const WalletStatus&)));

    connect(&_model, SIGNAL(onTxStatus(beam::ChangeAction, const std::vector<beam::TxDescription>&)),
        SLOT(onTxStatus(beam::ChangeAction, const std::vector<beam::TxDescription>&)));

    connect(&_model, SIGNAL(onChangeCalculated(beam::Amount)),
        SLOT(onChangeCalculated(beam::Amount)));

    connect(&_model, SIGNAL(onChangeCurrentWalletIDs(beam::WalletID, beam::WalletID)),
        SLOT(onChangeCurrentWalletIDs(beam::WalletID, beam::WalletID)));

    connect(&_model, SIGNAL(onAdrresses(bool, const std::vector<beam::WalletAddress>&)),
        SLOT(onAdrresses(bool, const std::vector<beam::WalletAddress>&)));

    connect(&_model, SIGNAL(onGeneratedNewAddress(const beam::WalletAddress&)),
        SLOT(onGeneratedNewAddress(const beam::WalletAddress&)));

    _model.getAsync()->getWalletStatus();
}

WalletViewModel::~WalletViewModel()
{

}

void WalletViewModel::cancelTx(TxObject* pTxObject)
{
    if (pTxObject->canCancel())
    {
        _model.getAsync()->cancelTx(pTxObject->_tx.m_txId);
    }
}

void WalletViewModel::deleteTx(TxObject* pTxObject)
{
    if (pTxObject->canDelete())
    {
        _model.getAsync()->deleteTx(pTxObject->_tx.m_txId);
    }
}

void WalletViewModel::generateNewAddress()
{
    _newReceiverAddr = {};
    _newReceiverName = "";

    _model.getAsync()->generateNewAddress();
}

void WalletViewModel::saveNewAddress()
{
    _newReceiverAddr.m_label = _newReceiverName.toStdString();
    if (_expires == 1)
    {
        _newReceiverAddr.m_duration = 0;
    }

    _model.getAsync()->saveAddress(_newReceiverAddr, true);
}

void WalletViewModel::copyToClipboard(const QString& text)
{
    QApplication::clipboard()->setText(text);
}

void WalletViewModel::onStatus(const WalletStatus& status)
{
    bool changed = false;

    if (_status.available != status.available)
    {
        _status.available = status.available;

        changed = true;

        emit actualAvailableChanged();
    }

    if (_status.received != status.received)
    {
        _status.received = status.received;

        changed = true;
    }

    if (_status.sent != status.sent)
    {
        _status.sent = status.sent;

        changed = true;
    }

    if (_status.unconfirmed != status.unconfirmed)
    {
        _status.unconfirmed = status.unconfirmed;

        changed = true;
    }

    if (_status.update.lastTime != status.update.lastTime)
    {
        _status.update.lastTime = status.update.lastTime;

        changed = true;
    }

    if (changed)
    {
        emit stateChanged();
    }
}

void WalletViewModel::onTxStatus(beam::ChangeAction action, const std::vector<TxDescription>& items)
{
    if (action == beam::ChangeAction::Reset)
    {
        _txList.clear();
        for (const auto& item : items)
        {
            _txList.push_back(new TxObject(item));
        }
    }
    else if (action == beam::ChangeAction::Removed)
    {
        for (const auto& item : items)
        {
            auto it = find_if(_txList.begin(), _txList.end(), [&item](const auto& tx) {return item.m_txId == tx->_tx.m_txId; });
            if (it != _txList.end())
            {
                _txList.erase(it);
            }
        }
    }
    else if (action == beam::ChangeAction::Updated)
    {
        auto txIt = _txList.begin();
        auto txEnd = _txList.end();
        for (const auto& item : items)
        {
            txIt = find_if(txIt, txEnd, [&item](const auto& tx) {return item.m_txId == tx->_tx.m_txId; });
            if (txIt == txEnd)
            {
                break;
            }
            (*txIt)->update(item);
        }
    }
    else if (action == beam::ChangeAction::Added)
    {
        // TODO in sort order
        for (const auto& item : items)
        {
            _txList.insert(0, new TxObject(item));
        }
    }

    sortTx();

    // Get info for TxObject::_user_name (get wallets labels)
    _model.getAsync()->getAddresses(false);

}

void WalletViewModel::onChangeCalculated(beam::Amount change)
{
    _change = change;
    emit actualAvailableChanged();
    emit changeChanged();
}

void WalletViewModel::onChangeCurrentWalletIDs(beam::WalletID senderID, beam::WalletID receiverID)
{
    //setSenderAddr(toString(senderID));
    setReceiverAddr(toString(receiverID));
}

QString WalletViewModel::available() const
{
    return BeamToString(_status.available);
}

QString WalletViewModel::received() const
{
    return BeamToString(_status.received);
}

QString WalletViewModel::sent() const
{
    return BeamToString(_status.sent);
}

QString WalletViewModel::unconfirmed() const
{
    return BeamToString(_status.unconfirmed);
}

QString WalletViewModel::sendAmount() const
{
    return _sendAmount;
}

QString WalletViewModel::feeGrothes() const
{
    return _feeGrothes;
}

QString WalletViewModel::getReceiverAddr() const
{
    return _receiverAddr;
}

void WalletViewModel::setReceiverAddr(const QString& value)
{
    auto trimmedValue = value.trimmed();
    if (_receiverAddr != trimmedValue)
    {
        _receiverAddr = trimmedValue;
        emit receiverAddrChanged();
    }
}

bool WalletViewModel::isValidReceiverAddress(const QString& value) {
    return _model.check_receiver_address(value.toStdString());
}

void WalletViewModel::setSendAmount(const QString& value)
{
    auto trimmedValue = value.trimmed();
    if (trimmedValue != _sendAmount)
    {
        _sendAmount = trimmedValue;
        _model.getAsync()->calcChange(calcTotalAmount());
        emit sendAmountChanged();
        emit actualAvailableChanged();
    }
}

void WalletViewModel::setFeeGrothes(const QString& value)
{
    auto trimmedValue = value.trimmed();
    if (trimmedValue != _feeGrothes)
    {
        _feeGrothes = trimmedValue;
        _model.getAsync()->calcChange(calcTotalAmount());
        emit feeGrothesChanged();
        emit actualAvailableChanged();
    }
}

void WalletViewModel::setComment(const QString& value)
{
    if (_comment != value)
    {
        _comment = value;
        emit commentChanged();
    }
}

QString WalletViewModel::getComment() const
{
	return _comment;
}

QString WalletViewModel::sortRole() const
{
    return _sortRole;
}

void WalletViewModel::setSortRole(const QString& value)
{
    if (value != getDateRole() && value != getAmountRole() &&
        value != getStatusRole() && value != getUserRole())
        return;

    _sortRole = value;
    sortTx();
}

Qt::SortOrder WalletViewModel::sortOrder() const
{
    return _sortOrder;
}

void WalletViewModel::setSortOrder(Qt::SortOrder value)
{
    _sortOrder = value;
    sortTx();
}

QString WalletViewModel::getIncomeRole() const
{
    return "income";
}

QString WalletViewModel::getDateRole() const
{
    return "date";
}

QString WalletViewModel::getUserRole() const
{
    return "user";
}

QString WalletViewModel::getDisplayNameRole() const
{
    return "displayName";
}

QString WalletViewModel::getAmountRole() const
{
    return "amount";
}

QString WalletViewModel::getStatusRole() const
{
    return "status";
}

int WalletViewModel::getDefaultFeeInGroth() const
{
    return kDefaultFeeInGroth;
}

void WalletViewModel::setExpires(int value)
{
    if (value != _expires)
    {
        _expires = value;
        emit expiresChanged();
    }
}

int WalletViewModel::getExpires() const
{
    return _expires;
}

QQmlListProperty<TxObject> WalletViewModel::getTransactions()
{
    return QQmlListProperty<TxObject>(this, _txList);
}

beam::Amount WalletViewModel::calcSendAmount() const
{
	return _sendAmount.toDouble() * Rules::Coin;
}

beam::Amount WalletViewModel::calcFeeAmount() const
{
    return _feeGrothes.toULongLong();
}

beam::Amount WalletViewModel::calcTotalAmount() const
{
    return calcSendAmount() + calcFeeAmount();
}

void WalletViewModel::sortTx()
{
    auto cmp = generateComparer();
    std::sort(_txList.begin(), _txList.end(), cmp);

    emit transactionsChanged();
}

std::function<bool(const TxObject*, const TxObject*)> WalletViewModel::generateComparer()
{
    if (_sortRole == getIncomeRole())
        return [sortOrder = _sortOrder](const TxObject* lf, const TxObject* rt)
    {
        return compareTx(lf->getTxDescription().m_sender, rt->getTxDescription().m_sender, sortOrder);
    };

    if (_sortRole == getUserRole())
        return [sortOrder = _sortOrder](const TxObject* lf, const TxObject* rt)
    {
        return compareTx(lf->user(), rt->user(), sortOrder);
    };

    if (_sortRole == getDisplayNameRole())
        return [sortOrder = _sortOrder](const TxObject* lf, const TxObject* rt)
    {
        return compareTx(lf->displayName(), rt->displayName(), sortOrder);
    };

    if (_sortRole == getAmountRole())
        return [sortOrder = _sortOrder](const TxObject* lf, const TxObject* rt)
    {
        return compareTx(lf->getTxDescription().m_amount, rt->getTxDescription().m_amount, sortOrder);
    };

    if (_sortRole == getStatusRole())
        return [sortOrder = _sortOrder](const TxObject* lf, const TxObject* rt)
    {
        return compareTx(lf->status(), rt->status(), sortOrder);
    };

    // defult for dateRole
    return [sortOrder = _sortOrder](const TxObject* lf, const TxObject* rt)
    {
        return compareTx(lf->getTxDescription().m_createTime, rt->getTxDescription().m_createTime, sortOrder);
    };
}

void WalletViewModel::sendMoney()
{
    if (/*!_senderAddr.isEmpty() && */isValidReceiverAddress(getReceiverAddr()))
    {
        WalletAddress peerAddr;
        peerAddr.m_walletID.FromHex(getReceiverAddr().toStdString());
        peerAddr.m_createTime = getTimestamp();
        peerAddr.m_label = _comment.toStdString();

        // TODO: implement UI for this situation
        // TODO: don't save if you send to yourself
        _model.getAsync()->saveAddress(peerAddr, false);

        // TODO: show 'operation in process' animation here?
        _model.getAsync()->sendMoney(peerAddr.m_walletID, _comment.toStdString(), calcSendAmount(), calcFeeAmount());
    }
}

void WalletViewModel::syncWithNode()
{
    //setIsSyncInProgress(true);
    _model.getAsync()->syncWithNode();
}

QString WalletViewModel::actualAvailable() const
{
    return BeamToString(_status.available - calcTotalAmount() - _change);
}

bool WalletViewModel::isEnoughMoney() const
{
    return _status.available >= calcTotalAmount() + _change;
}

QString WalletViewModel::change() const
{
    return BeamToString(_change);
}

QString WalletViewModel::getNewReceiverAddr() const
{
    return toString(_newReceiverAddr.m_walletID);
}

QString WalletViewModel::getNewReceiverAddrQR() const
{
    return _newReceiverAddrQR;
}

void WalletViewModel::setNewReceiverName(const QString& value)
{
    auto trimmedValue = value.trimmed();
    if (_newReceiverName != trimmedValue)
    {
        _newReceiverName = trimmedValue;
        emit newReceiverNameChanged();
    }
}

QString WalletViewModel::getNewReceiverName() const
{
	return _newReceiverName;
}

void WalletViewModel::onAdrresses(bool own, const std::vector<beam::WalletAddress>& addresses)
{
    if (own)
    {
        return;
    }

    for (auto* tx : _txList)
    {
        auto foundIter = std::find_if(addresses.cbegin(), addresses.cend(),
                                      [tx](const auto& address) { return address.m_walletID == tx->peerId(); });

        if (foundIter != addresses.cend())
        {
            tx->setUserName(QString::fromStdString(foundIter->m_label));
        }
        else if (!tx->userName().isEmpty())
        {
            tx->setUserName(QString{});
        }

        auto displayName = tx->userName().isEmpty() ? tx->user() : tx->userName();
        tx->setDisplayName(displayName);
    }
}

void WalletViewModel::onGeneratedNewAddress(const beam::WalletAddress& addr)
{
    _newReceiverAddr = addr;
    _newReceiverAddrQR = "";
    setExpires(0);

    CQR_Encode qrEncode;
    QString strAddr(toString(_newReceiverAddr.m_walletID));
    bool success = qrEncode.EncodeData(1, 0, true, -1, strAddr.toUtf8().data());

    if (success)
    {
        int qrImageSize = qrEncode.m_nSymbleSize;
        int encodeImageSize = qrImageSize + (QR_MARGIN * 2);
        QImage encodeImage(encodeImageSize, encodeImageSize, QImage::Format_ARGB32);
        encodeImage.fill(Qt::transparent);
        QColor color(Qt::white);

        for (int i = 0; i < qrImageSize; i++)
            for (int j = 0; j < qrImageSize; j++)
                if (qrEncode.m_byModuleData[i][j])
                    encodeImage.setPixel(i + QR_MARGIN, j + QR_MARGIN, color.rgba());

        encodeImage = encodeImage.scaled(200, 200);

        QByteArray bArray;
        QBuffer buffer(&bArray);
        buffer.open(QIODevice::WriteOnly);
        encodeImage.save(&buffer, "png");

        _newReceiverAddrQR = "data:image/png;base64,";
        _newReceiverAddrQR.append(QString::fromLatin1(bArray.toBase64().data()));
    }

    emit newReceiverAddrChanged();
}
