#include "Board.h"
#include "ui_Board.h"

#include "Piece.h"
#include "TurnManager.h"

#include <QDebug>
#include <QMessageBox>
#include <QMutexLocker>

// Static member definitions
boardStateMapType Board::_workingBoardStateMap  = boardStateMapType();
boardStateMapType Board::_backedUpBoardStateMap = boardStateMapType();
boardStateMapType Board::_stagingBoardStateMap  = boardStateMapType();

piecesSetType Board::_workingCapturedPieces  = piecesSetType();
piecesSetType Board::_backedUpCapturedPieces = piecesSetType();
piecesSetType Board::_stagingCapturedPieces  = piecesSetType();

Board::Board(QWidget* parent):
  QWidget(parent),
  ui(new Ui::Board)
{
  ui->setupUi(this);
  resetBoard(true, true);
}

Board::~Board()
{

}

void Board::updatePieceMap(Cell* from, Cell* to, boardStateMapType& boardStateMap, piecesSetType& capturedPiecesContainer)
{
  boardCoordinateType fromCoords = boardCoordinateType(from->row(), from->column());
  definedPieceType fromType = definedPieceType(from->assignedPiece()->identity(), from->assignedPiece()->color());

  boardCoordinateType toCoords = boardCoordinateType(to->row(), to->column());
  definedPieceType toType = definedPieceType(to->assignedPiece()->identity(), to->assignedPiece()->color());

  // check if this is an attack
  if (toType.first != Pieces::Identities::eNone) {
    // It's an attack
    // Destination piece is now considered to be captured
    capturedPiecesContainer.insert(toType);
    boardStateMap.remove(toCoords);
  }

  Q_ASSERT_X(boardStateMap.contains(fromCoords), "updatePieceMap", "From-position not found in map!");

  boardStateMap.take(fromCoords); // takes the piece from its old location
  boardStateMap.insert(toCoords, fromType); // puts it in its new location
}

void Board::clearHighLights()
{
  for (int row = 1; row <= 8; ++row)
    for (int column = 1; column <= 8; ++column) {

      Cell* cell = getCell(row, column);

      if (cell != nullptr) {

        cell->highLightCell(false);

      }
    }
}

void Board::moveInitiated(boardCoordinateType fromWhere)
{
  // validate that there is actually a piece there on the board.
  if (getCell(fromWhere)->assignedPiece()->identity() == Pieces::Identities::eNone ||
      getCell(fromWhere)->assignedPiece()->color() == Pieces::PieceColors::eNone) {
    clearHighLights();
    uncheckAllCheckedCells();
    return;
  }

  // Check that the right user is attempting to move
  if (TurnManager::currentPlayer()->identity() == UserIdentity::eHuman) {
    // Pieces must be white
    if (getCell(fromWhere)->assignedPiece()->color() != Pieces::PieceColors::eWhite) {
      clearHighLights();
      uncheckAllCheckedCells();
      return;
    }
  }

  if (TurnManager::currentPlayer()->identity() == UserIdentity::eComputer) {
    // Pieces must be Black
    if (getCell(fromWhere)->assignedPiece()->color() != Pieces::PieceColors::eBlack) {
      clearHighLights();
      uncheckAllCheckedCells();
      return;
    }
  }

  boardCoordinatesType containerForHighlighting = boardCoordinatesType();

  // If we reach this point, it is a real piece, and it is the right color for the user trying to move it.
  // Let's evaluate the current boardState

  bool boardIsValid = evaluateBoardState(_workingBoardStateMap);

  // This would imply the King of the current player is checked, so valid moves
  if (!boardIsValid) {
    // see if any potential moves can bring the board into a legal state (uncheck the king)
    // first check if the selected cell can attack the offender

    MoveRules::movementType rules = MoveRules::getMovementRules(getCell(fromWhere)->assignedPiece()->identity(), getCell(fromWhere)->assignedPiece()->color());
    definedPieceType currentPiece = definedPieceType(getCell(fromWhere)->assignedPiece()->identity(), getCell(fromWhere)->assignedPiece()->color());
    boardCoordinatesType container = boardCoordinatesType();

    // Map it's possible moves.  mapMoves() only returns "legal" moves
    mapMoves(rules, currentPiece, container, fromWhere);

    if (getCell(fromWhere)->assignedPiece()->identity() == Pieces::Identities::eKnight) {
      // Knight's path is L-shaped, not straight horizontal/vertical/diagonal like the other pieces
      // It is a piece, it is the right color.  Map it's move-rules

      if (!container.isEmpty()) {
        bool canItBeAttacked = container.contains(_locationOfAttacker);
        if (canItBeAttacked) {
          // Attack the bugger
        }
        else {
          // try to block his path
          boardCoordinatesType set = getPath(_locationOfVictim, _locationOfAttacker, _workingBoardStateMap);
          bool pathCanBePotentiallyBlocked = !set.isEmpty();
          if (pathCanBePotentiallyBlocked) {
            boardCoordinatesType possibleMoves = set.intersect(container);
            if (!possibleMoves.isEmpty()) {
              // record the starting-cell, highlight the outcomes
              _locationStart = fromWhere;
              containerForHighlighting = possibleMoves;
            }
            else {
              // Selected piece has no available moves.
            }
          }
        }
      }
    }
    else {
      boardCoordinatesType set = getPath(fromWhere, _locationOfAttacker, _workingBoardStateMap);

      bool canItBeAttacked = !set.isEmpty();
      // If we can attack it directly, there will be a path to it.
      if (canItBeAttacked) {
        // Highlight the way
        _locationStart = fromWhere;
        containerForHighlighting = set;
      }
      else { // can we block its path?
        set = getPath(_locationOfVictim, _locationOfAttacker, _workingBoardStateMap);
        bool pathCanBePotentiallyBlocked = !set.isEmpty();
        if (pathCanBePotentiallyBlocked) {
          boardCoordinatesType possibleMoves = set.intersect(container);
          if (!possibleMoves.isEmpty()) {
            // record the starting-cell, highlight the outcomes
            _locationStart = fromWhere;
            containerForHighlighting = possibleMoves;
          }
          else {
            // Selected piece has no available moves.
          }
        }
      }
    }
  }
  else {
    // Board is in a valid state (your king is not in danger)
    MoveRules::movementType rules = MoveRules::getMovementRules(getCell(fromWhere)->assignedPiece()->identity(), getCell(fromWhere)->assignedPiece()->color());
    definedPieceType currentPiece = definedPieceType(getCell(fromWhere)->assignedPiece()->identity(), getCell(fromWhere)->assignedPiece()->color());
    boardCoordinatesType container = boardCoordinatesType();

    // Map it's possible moves.  mapMoves() only returns "legal" moves
    mapMoves(rules, currentPiece, container, fromWhere);


    if (!container.isEmpty()) {
      // One last check to see if any of the proposed moves will in fact result in an invalid board state.
      boardCoordinatesType::iterator containerIterator = container.begin();
      while (containerIterator != container.end()) {
        boardStateMapType tempState = _workingBoardStateMap;
        piecesSetType tempPieces = _workingCapturedPieces;
        boardCoordinateType toWhere = *containerIterator;
        Cell* from = getCell(fromWhere);
        Cell* to = getCell(toWhere);
        movePieceStart(this, from, to, tempState, tempPieces);

        bool boardStillValid = evaluateBoardState(tempState);
        if (!boardStillValid) { // this will check the current player's king
          container.remove(toWhere);
          containerIterator = container.begin();
        }
        else {
          ++containerIterator;
        }
        movePieceRevertMove(tempState, tempPieces);
      }

      if (!container.isEmpty()) {
        _locationStart = fromWhere;
        containerForHighlighting = container;
      }
    }
  }

  if (containerForHighlighting.isEmpty()) {
    clearHighLights();
    uncheckAllCheckedCells();
    return;
  }
  else {
    highLightCoordinates(containerForHighlighting);
    _containerForMoving = containerForHighlighting;
  }

  emit moveInitiatedComplete(TurnManager::getInstance().currentPlayer());
}

void Board::continueInitiatedMove(boardCoordinateType whereTo)
{
  _locationEnd = whereTo;

  // check if the move will be allowed
  if (_containerForMoving.contains(whereTo)) {
    Cell* whereFrom = getCell(_locationStart);
    Cell* whereTo =   getCell(_locationEnd);
    movePieceStart(this, whereFrom, whereTo);
    movePieceCompleteMove(this);

    if(TurnManager::getInstance().currentPlayer()->identity() == UserIdentity::eHuman)
    {
      TurnManager::switchPlayers(_player2);
    }
    else
    {
      TurnManager::switchPlayers(_player1);
    }

  }
  else {
    clearHighLights();
    uncheckAllCheckedCells();
  }
}

void Board::handleMoveInitiatedComplete(QSharedPointer<Player>& playerWhoInitiated)
{
  if(playerWhoInitiated->identity() == player2()->identity())
  {
    emit aiMoveCompletionRequired();
  }
}
definedPieceType Board::pieceWhoWillBeAttacked() const
{
  return _pieceWhoWillBeAttacked;
}

void Board::setPieceWhoWillBeAttacked(const definedPieceType& pieceWhoWillBeAttacked)
{
  _pieceWhoWillBeAttacked = pieceWhoWillBeAttacked;
}

definedPieceType Board::pieceWhoWillBeAttacking() const
{
  return _pieceWhoWillBeAttacking;
}

void Board::setPieceWhoWillBeAttacking(const definedPieceType& pieceWhoWillBeAttacking)
{
  _pieceWhoWillBeAttacking = pieceWhoWillBeAttacking;
}

boardCoordinateType Board::locationOfVictim() const
{
  return _locationOfVictim;
}

void Board::setLocationOfVictim(const boardCoordinateType& locationOfVictim)
{
  _locationOfVictim = locationOfVictim;
}

boardCoordinateType Board::locationOfAttacker() const
{
  return _locationOfAttacker;
}

void Board::setLocationOfAttacker(const boardCoordinateType& locationOfAttacker)
{
  _locationOfAttacker = locationOfAttacker;
}


QSharedPointer<Player>& Board::player2()
{
  return _player2;
}

void Board::setPlayer2(const QSharedPointer<Player>& player2)
{
  _player2 = player2;
}

QSharedPointer<Player>& Board::player1()
{
  return _player1;
}

void Board::setPlayer1(const QSharedPointer<Player>& player1)
{
  _player1 = player1;
}


boardCoordinateType Board::findPiece(Pieces::PieceColors::ePieceColors colorThatIsToBeFound,
                                     Pieces::Identities::eIdentities identityThatIsToBeFound)
{
  definedPieceType pieceType = definedPieceType(identityThatIsToBeFound, colorThatIsToBeFound);
  return findPiece(pieceType);
}

boardCoordinateType Board::findPiece(definedPieceType piece)
{
  boardCoordinateType pieceLocation = boardCoordinateType(0, 0);
  bool foundIt = false;
  // Iterate through the current board map to see if any enemy piece can reach the piece
  // described by color and identity
  for (int row = eMinRow; row <= eMaxRow; ++row) {
    if (foundIt) {
      break;
    }

    for (int column = eMinColumn; column <= eMaxColumn; ++column) {
      boardCoordinateType currentCoordinate = boardCoordinateType(row, column);
      definedPieceType currentPiece = _workingBoardStateMap.value(currentCoordinate);

      Pieces::Identities::eIdentities currentPieceIdentity = currentPiece.first;
      Pieces::PieceColors::ePieceColors currentPieceColor = currentPiece.second;

      if (currentPieceIdentity == Pieces::Identities::eNone) {
        // No piece on this cell
        continue;
      }
      if (currentPieceColor != piece.second) {
        // Wrong color for piece
        continue;
      }
      if (currentPieceIdentity != piece.first) {
        // Right color, not an empty cell, wrong identity
        continue;
      }

      // Reaching this point we now have the location of the piece.
      pieceLocation = currentCoordinate;
      foundIt = true;
      break;
    }
  }
  return pieceLocation;
}

boardCoordinatesType Board::findPieces(definedPieceType piece, boardStateMapType& boardStateToSearch)
{
  boardCoordinatesType retVal;
  if (piece.first == Pieces::Identities::eKing ||
      piece.first == Pieces::Identities::eQueen) { // Only one of each
    boardCoordinateType partOfRetVal = findPiece(piece);
    retVal.insert(partOfRetVal);
    return retVal;
  }

  boardCoordinateType pieceLocation = boardCoordinateType(0, 0);

  // Iterate through the current board map to see if any enemy piece can reach the piece
  // described by color and identity
  for (int row = eMinRow; row <= eMaxRow; ++row) {
    for (int column = eMinColumn; column <= eMaxColumn; ++column) {
      boardCoordinateType currentCoordinate = boardCoordinateType(row, column);
      definedPieceType currentPiece = _workingBoardStateMap.value(currentCoordinate);

      Pieces::Identities::eIdentities currentPieceIdentity = currentPiece.first;
      Pieces::PieceColors::ePieceColors currentPieceColor = currentPiece.second;

      if (currentPieceIdentity == Pieces::Identities::eNone) {
        // No piece on this cell
        continue;
      }
      if (currentPieceColor != piece.second) {
        // Wrong color for piece
        continue;
      }
      if (currentPieceIdentity != piece.first) {
        // Right color, not an empty cell, wrong identity
        continue;
      }

      // Reaching this point we now have the location of the piece.
      pieceLocation = currentCoordinate;
      retVal.insert(pieceLocation);
    }
  }
  return retVal;
}

void Board::highLightCoordinates(boardCoordinatesType& set)
{
  boardCoordinatesType::iterator i = set.begin();

  while (i != set.end()) {
    boardCoordinateType coordinate = *i;
    ++i;

    Cell* toCell = getCell(coordinate.first, coordinate.second);

    toCell->highLightCell(true);
  }
}

void Board::toggleCell(Cell* cell)
{
  cell->toggle();
}

boardCoordinatesType Board::getPath(boardCoordinateType pointA, boardCoordinateType pointB, boardStateMapType& boardStateToSearch)
{
  boardCoordinatesType returnSet;
  int rowA = pointA.first;
  int columnA = pointA.second;

  int rowB = pointB.first;
  int columnB = pointB.second;

  int rowMax = rowA > rowB ? rowA : rowB;
  int columnMax = columnA > columnB ? columnA : columnB;
  int rowMin = rowA < rowB ? rowA : rowB;
  int columnMin = columnA < columnB ? columnA : columnB;

  // Make a few practical decisions.
  if (rowMin == rowMax) { // The piece moves horizontally as the row value doesn't change
    // Only need to iterate through the x-axis
    for (int i = columnMin + 1; i <= columnMax; ++i) {
      boardCoordinateType temp = boardCoordinateType(rowMax, i);
      if (boardStateToSearch.value(temp).first != Pieces::Identities::eNone) {
        // There is a piece between you and your destination.
        if (i != columnMax) { // If it is not the destination block, somebody is in your way.
          returnSet = boardCoordinatesType();
          return returnSet;
        }
        else {
          boardCoordinateType validCoordinate = boardCoordinateType(rowMax, i);
          returnSet.insert(validCoordinate);
        }
        // Already checked the source and destination piece colors
        // If the identity of the piece on the destination block is not eNone, the move is legal
      }
      else {
        boardCoordinateType validCoordinate = boardCoordinateType(rowMax, i);
        returnSet.insert(validCoordinate);
      }
    }
    // The for-loop ran it's course, and the return wasn't called.  I call that a success
    return returnSet;
  }

  if (columnMin == columnMax) { // The piece moves horizontally as the y-axis value doesn't change
    // Only need to iterate through the x-axis
    for (int j = rowMin + 1; j <= rowMax; ++j) {
      boardCoordinateType temp = boardCoordinateType(j, columnMax);
      if (boardStateToSearch.value(temp).first != Pieces::Identities::eNone) {
        // There is a piece between you and your destination.
        if (j != rowMax) { // If it is not the destination block, somebody is in your way.
          returnSet = boardCoordinatesType();
          return returnSet;
        }
        else {
          boardCoordinateType validCoordinate = boardCoordinateType(j, columnMax);
          returnSet.insert(validCoordinate);
        }
        // Already checked the source and destination piece colors
        // If the identity of the piece on the destination block is not eNone, the move is legal
      }
      else {
        boardCoordinateType validCoordinate = boardCoordinateType(j, columnMax);
        returnSet.insert(validCoordinate);
      }
    }
    // The for-loop ran it's course, and the return wasn't called.  I call that a success
    return returnSet;
  }

  // If we reach this line, it means the move is diagonal.  Apart from the Knight piece
  // the difference between xMin/xMax and yMin/yMax should be the same, so check that
  if ((rowMax - rowMin) != (columnMax - columnMin)) { // The poo has hitteth the proverbial fan
    returnSet = boardCoordinatesType();
    return returnSet;
  }

  int stepFactorRows = (rowA < rowB) ? 1 : -1;
  int stepFactorColumns = (columnA < columnB) ? 1 : -1;
  int newColumn = columnA;

  // Special case
  if (rowA + stepFactorRows == rowB) {
    if (newColumn + stepFactorColumns == columnB) {
      boardCoordinateType validCoordinate = boardCoordinateType(rowB, columnB);
      returnSet.insert(validCoordinate);
    }
  }

  for (int row = rowA + stepFactorRows; row != rowB; row += stepFactorRows) {
    for (int col = newColumn + stepFactorColumns; col != columnB; col += stepFactorColumns) {
      boardCoordinateType temp = boardCoordinateType(row, col);
      if (boardStateToSearch.value(temp).first != Pieces::Identities::eNone) {
        // There is a piece between you and your destination.
        if (col != columnB - stepFactorColumns && row != rowB - stepFactorRows) { // If it is not the destination block, somebody is in your way.
          returnSet = boardCoordinatesType();
          return returnSet;
        }
        // Already checked the source and destination piece colors
        // If the identity of the piece on the destination block is not eNone, the move is legal
      }
      else {
        boardCoordinateType validCoordinate = boardCoordinateType(row, col);
        returnSet.insert(validCoordinate);
      }

      newColumn += stepFactorColumns;
      break; // ensure x and y step together
    }
  }
  // The for-loop ran it's course, and the return wasn't called.  I call that a success
  return returnSet;
}

bool Board::evaluateBoardState(boardStateMapType& boardStateToEvaluate)
{
  boardCoordinatesType container;
  Pieces::PieceColors::ePieceColors color;
  if (TurnManager::currentPlayer()->identity() == UserIdentity::eHuman) {
    color = Pieces::PieceColors::eWhite;
  }
  else {
    color = Pieces::PieceColors::eBlack;
  }

  bool kingIsChecked = isTheTargetWithinRange(color, Pieces::Identities::eKing, container, boardStateToEvaluate);

  if (kingIsChecked) {
    return false;
  }

  return true;
}

bool Board::isTheTargetWithinRange(Pieces::PieceColors::ePieceColors colorThatIsToBeAttacked,
                                   Pieces::Identities::eIdentities identityThatIsToBeAttacked,
                                   boardCoordinatesType& container,
                                   boardStateMapType& boardStateToUse)
{
  Pieces::PieceColors::ePieceColors attackerColor = Pieces::flipColor(colorThatIsToBeAttacked);
  boardCoordinatesType targetsLocation;
  boardCoordinateType targetLocation = boardCoordinateType(0, 0);

  boardCoordinateType failure = boardCoordinateType(0, 0);
  targetsLocation = findPieces(definedPieceType(identityThatIsToBeAttacked, colorThatIsToBeAttacked), boardStateToUse);
  if (targetsLocation.contains(failure)) {
    return false;
  }

  boardCoordinatesType::iterator boardCoordsIterator;
  bool useIterator = false;
  boardCoordsIterator = targetsLocation.begin();
  useIterator = true;


  for (int i = 0; i < targetsLocation.size(); ++i) {

    targetLocation = *boardCoordsIterator;
    ++boardCoordsIterator;

    // So now that we know where the target is, iterate through all enemy pieces, map their moves and
    // see if any legal move includes the target's location.
    for (int row = eMinRow; row <= eMaxRow; ++row) {
      for (int column = eMinColumn; column <= eMaxColumn; ++column) {
        boardCoordinateType currentCoordinate = boardCoordinateType(row, column);
        definedPieceType currentPiece = boardStateToUse.value(currentCoordinate);

        Pieces::Identities::eIdentities currentPieceIdentity = currentPiece.first;
        Pieces::PieceColors::ePieceColors currentPieceColor = currentPiece.second;

        if (currentPieceIdentity == Pieces::Identities::eNone) {
          // No piece on this cell
          continue;
        }
        if (currentPieceColor != attackerColor) {
          // Wrong color for attacker
          continue;
        }
        // It is a piece, it is the right color.  Map it's moves
        MoveRules::movementType rules = MoveRules::getMovementRules(currentPieceIdentity, currentPieceColor);

        mapMoves(rules, currentPiece, container, currentCoordinate);
        definedPieceType pieceToAttack = definedPieceType(identityThatIsToBeAttacked,
                                                          colorThatIsToBeAttacked); //_currentBoardStateMap.value(currentCoordinate);

        if (container.contains(targetLocation)) {
          _locationOfAttacker      = currentCoordinate;
          _locationOfVictim        = targetLocation;
          _pieceWhoWillBeAttacking = currentPiece;
          _pieceWhoWillBeAttacked  = pieceToAttack;
          return true;
        }
      }
    }
  }
  container.clear();
  return false;
}

void Board::movePieceStart(Board* _this, Cell* fromCell, Cell* toCell, boardStateMapType& scenario, piecesSetType& scenarioPieces)
{
  // back up previous state
  _backedUpBoardStateMap = boardStateMapType(scenario);
  _backedUpCapturedPieces = piecesSetType(scenarioPieces);

  // ensure the future starts with the scenario
  _stagingBoardStateMap = boardStateMapType(scenario);
  _stagingCapturedPieces = piecesSetType(scenarioPieces);

  // Update the staging map
//  updatePieceMap(getCell(from.first, from.second), getCell(to.first, to.second), _stagingBoardStateMap, _stagingCapturedPieces);
  _this->updatePieceMap(fromCell, toCell, _stagingBoardStateMap, _stagingCapturedPieces);

  // transfer the staged state into the scenario
  scenario = boardStateMapType(_stagingBoardStateMap);
  scenarioPieces = piecesSetType(_stagingCapturedPieces);
}

void Board::movePieceCompleteMove(Board* _this, boardStateMapType& scenario)
{
  _this->redrawBoardFromMap(scenario);
  _this->clearHighLights();
  _this->uncheckAllCheckedCells();
}

void Board::movePieceRevertMove(boardStateMapType& scenario, piecesSetType& scenarioPieces)
{
  // recover from backup
  scenario = boardStateMapType(_backedUpBoardStateMap);
  scenarioPieces = piecesSetType(_backedUpCapturedPieces);
}

void Board::mapMoves(MoveRules::movementType rules, definedPieceType piece, boardCoordinatesType& container, boardCoordinateType location)
{
  bool lessLinearMoveRequired = false;

  if (!container.isEmpty()) { // QSet < QPair < row, column> >
    container.clear();
  }

  int startRow = location.first;
  int startColumn = location.second;

  MoveRules::directionsType moveDirections = rules.first;
  MoveRules::magnitudesType moveMagnitudes = rules.second;
  MoveRules::directionType firstMoveDirections = moveDirections.first;
  //  MoveRules::directionType secondMoveDirections = moveDirections.second;
  MoveRules::magnitudeType firstMagnitude = moveMagnitudes.first;
  MoveRules::magnitudeType secondMagnitude = moveMagnitudes.second;

  // firstMoveDirections are for all pieces
  // secondMoveDirections are only for Knights
  // firstMagnitude is mainly a minimum and secondMagnitude is a maximum
  // except when used for a knight

  if (piece.first == Pieces::Identities::eKnight) {
    lessLinearMoveRequired = true;
  }

  if (piece.first == Pieces::Identities::ePawn) {
    if (piece.second == Pieces::PieceColors::eBlack) {
      if (location.first > 2) {
        secondMagnitude = 1;
      }
      else {
        secondMagnitude = 2;
      }
    }
    else {
      if (location.first < 7) {
        secondMagnitude = 1;
      }
      else {
        secondMagnitude = 2;
      }
    }
  }

  if (!lessLinearMoveRequired) {

    MoveRules::directionType::iterator i = firstMoveDirections.begin();

    while (i != firstMoveDirections.end()) {

      MoveRules::Direction::eDirectionRules temp = *i;
      ++i;

      switch (temp) {
      case MoveRules::Direction::eMayMoveNorth      : {
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startRow - x >= 1) {
            boardCoordinateType coord = boardCoordinateType(startRow - x, startColumn);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      }
      case MoveRules::Direction::eMayMoveNorthEast  :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startRow - x >= 1 && startColumn + x <= 8) {
            boardCoordinateType coord = boardCoordinateType(startRow - x, startColumn + x);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      case MoveRules::Direction::eMayMoveEast       :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startColumn + x <= 8) {
            boardCoordinateType coord = boardCoordinateType(startRow, startColumn + x);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      case MoveRules::Direction::eMayMoveSouthEast  :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startRow + x <= 8 && startColumn + x <= 8) {
            boardCoordinateType coord = boardCoordinateType(startRow + x, startColumn + x);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      case MoveRules::Direction::eMayMoveSouth      :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startRow + x <= 8) {
            boardCoordinateType coord = boardCoordinateType(startRow + x, startColumn);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      case MoveRules::Direction::eMayMoveSouthWest  :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startRow + x <= 8 && startColumn - x >= 1) {
            boardCoordinateType coord = boardCoordinateType(startRow + x, startColumn - x);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      case MoveRules::Direction::eMayMoveWest       :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startColumn - x >= 1) {
            boardCoordinateType coord = boardCoordinateType(startRow, startColumn - x);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      case MoveRules::Direction::eMayMoveNorthWest  :
        for (int x = firstMagnitude; x <= secondMagnitude; ++x) {
          if (startRow - x >= 1 && startColumn - x >= 1) {
            boardCoordinateType coord = boardCoordinateType(startRow - x, startColumn - x);
            container.insert(coord);
          }
          else {
            break;
          }
        }
        break;
      }
    }
  }
  else { // This is a knight, he doesn't move quite as linear as the other pieces.

    MoveRules::directionType::iterator i = firstMoveDirections.begin();

    while (i != firstMoveDirections.end()) {

      MoveRules::Direction::eDirectionRules temp = *i;
      ++i;

      switch (temp) {
      case MoveRules::Direction::eMayMoveNorthEast  :
        // Knight can move 2 East and 1 North
        if (startRow - 1 >= 1 && startColumn + 2 <= 8) {
          boardCoordinateType coord = boardCoordinateType(startRow - 1, startColumn + 2);
          container.insert(coord);
        }
        // Knight can move 1 East and 2 North
        if (startRow - 2 >= 1 && startColumn + 1 <= 8) {
          boardCoordinateType coord = boardCoordinateType(startRow - 2, startColumn + 1);
          container.insert(coord);
        }
        break;
      case MoveRules::Direction::eMayMoveSouthEast  :
        // Knight can move 2 East and 1 South
        if (startRow + 1 <= 8 && startColumn + 2 <= 8) {
          boardCoordinateType coord = boardCoordinateType(startRow + 1, startColumn + 2);
          container.insert(coord);
        }
        // Knight can move 1 East and 2 South
        if (startRow + 2 <= 8 && startColumn + 1 <= 8) {
          boardCoordinateType coord = boardCoordinateType(startRow + 2, startColumn + 1);
          container.insert(coord);
        }
        break;
      case MoveRules::Direction::eMayMoveSouthWest  :
        // Knight can move 2 West and 1 South
        if (startRow + 1 <= 8 && startColumn - 2 >= 1) {
          boardCoordinateType coord = boardCoordinateType(startRow + 1, startColumn - 2);
          container.insert(coord);
        }
        // Knight can move 1 West and 2 South
        if (startRow + 2 <= 8 && startColumn - 1 >= 1) {
          boardCoordinateType coord = boardCoordinateType(startRow + 2, startColumn - 1);
          container.insert(coord);
        }
        break;
      case MoveRules::Direction::eMayMoveNorthWest  :
        // Knight can move 2 West and 1 North
        if (startRow - 1 >= 1 && startColumn - 2 >= 1) {
          boardCoordinateType coord = boardCoordinateType(startRow - 1, startColumn - 2);
          container.insert(coord);
        }
        // Knight can move 1 West and 2 North
        if (startRow - 2 >= 1 && startColumn - 1 >= 1) {
          boardCoordinateType coord = boardCoordinateType(startRow - 2, startColumn - 1);
          container.insert(coord);
        }
        break;
      }
    }
  }

  // moves mapped, now remove all the impossible ones
  if (!container.isEmpty()) {
    boardCoordinatesType::iterator i = container.begin();

    while (i != container.end()) {
      boardCoordinateType coordinate = *i;
      ++i;

      if (!isMoveLegal(location, coordinate, container)) {
        container.remove(coordinate);
        i = container.begin();
      }
    }
  }
}

bool Board::isMoveLegal(boardCoordinateType moveFrom, boardCoordinateType moveTo, boardCoordinatesType& containerToUse)
{
  Pieces::PieceColors::ePieceColors fromColor = _workingBoardStateMap.value(moveFrom).second;
  Pieces::PieceColors::ePieceColors toColor = _workingBoardStateMap.value(moveTo).second;

  if (containerToUse.isEmpty()) {
    return false; // No possible moves for selected piece.
  }

  boardCoordinateType chosenSpot = moveTo;
  // Is the choice in the scope of possible moves?  Does the destination contain one of my own pieces?
  if (containerToUse.contains(chosenSpot) && fromColor != toColor) {

    // Now we have determined the selected piece can travel to the chosen spot (cell) through
    // it's legal ranges of movement, but, we don't yet know if it can get there (obstructions?)

    int x1 = moveFrom.second;
    int y1 = moveFrom.first;

    int x2 = moveTo.second;
    int y2 = moveTo.first;

    int xMax = x1 > x2 ? x1 : x2;
    int yMax = y1 > y2 ? y1 : y2;
    int xMin = x1 < x2 ? x1 : x2;
    int yMin = y1 < y2 ? y1 : y2;

    // If the piece in question is a Pawn, and the FROM and TO columns are not the same, there has to be an
    // enemy piece on the destination cell.  Do a quick check and disqualify if needed.
    if (_workingBoardStateMap.value(moveFrom).first == Pieces::Identities::ePawn) {
      if (x1 != x2) {

        // Tried to move horizontally by more than one cell.  Will not be allowed
        if (xMax - xMin > 1) {
          return false;
        }

        // Tried to move sideways (attack), but no enemy on that cell.  Will not be allowed.
        if (fromColor == Pieces::PieceColors::eBlack && toColor != Pieces::PieceColors::eWhite) {
          return false;
        }
        else if (fromColor == Pieces::PieceColors::eWhite && toColor != Pieces::PieceColors::eBlack) {
          return false;
        }
        else {
          return true;
        }

      }
      else {  // Straight move, can only attack sideways, so if destination cell contains
        // an enemy piece, thwart it
        if (toColor != Pieces::PieceColors::eNone) {
          return false;
        }

        // If the code execution reaches this point, we can safely assume the pawn is moving
        // in a single column, and that the destination cell does not contain an enemy.
        // Allow it.
        return true;
      }
    }

    // Knights can "jump over" other pieces, so not going to check if anybody is in his way.
    if (_workingBoardStateMap.value(moveFrom).first != Pieces::Identities::eKnight) {

      // Make a few practical decisions.
      if (xMin == xMax) { // The piece moves vertically as the x-axis value doesn't change
        // Only need to iterate through the y-axis
        for (int i = yMin + 1; i <= yMax; ++i) {
          boardCoordinateType temp = boardCoordinateType(i, xMax);
          if (_workingBoardStateMap.value(temp).first != Pieces::Identities::eNone) {
            // There is a piece between you and your destination.
            if (i != yMax) { // If it is not the destination block, somebody is in your way.
              return false;
            }
            // Already checked the source and destination piece colors
            // If the identity of the piece on the destination block is not eNone, the move is legal
          }
        }
        // The for-loop ran it's course, and the return wasn't called.  I call that a success
        return true;
      }

      if (yMin == yMax) { // The piece moves horizontally as the y-axis value doesn't change
        // Only need to iterate through the x-axis
        for (int j = xMin + 1; j <= xMax; ++j) {
          boardCoordinateType temp = boardCoordinateType(yMax, j);
          if (_workingBoardStateMap.value(temp).first != Pieces::Identities::eNone) {
            // There is a piece between you and your destination.
            if (j != xMax) { // If it is not the destination block, somebody is in your way.
              return false;
            }
            // Already checked the source and destination piece colors
            // If the identity of the piece on the destination block is not eNone, the move is legal
          }
        }
        // The for-loop ran it's course, and the return wasn't called.  I call that a success
        return true;
      }

      // If we reach this line, it means the move is diagonal.  Apart from the Knight piece
      // the difference between xMin/xMax and yMin/yMax should be the same, so check that
      if ((xMax - xMin) != (yMax - yMin)) { // The poo has hitteth the proverbial fan
        return false;
      }

      // Finally, once we reach this line, the check should be easy
      // Few simple checks
      int stepFactorX = (x1 < x2) ? 1 : -1;
      int stepFactorY = (y1 < y2) ? 1 : -1;
      int newY = y1;

      for (int col = x1 + stepFactorX; col != x2 + stepFactorX; col += stepFactorX) {
        for (int row = newY + stepFactorY; row != y2 + stepFactorY; row += stepFactorY) {
          boardCoordinateType temp = boardCoordinateType(row, col);
          if (_workingBoardStateMap.value(temp).first != Pieces::Identities::eNone) {
            // There is a piece between you and your destination.
            if (col != x2 && row != y2) { // If it is not the destination block, somebody is in your way.
              return false;
            }
            // Already checked the source and destination piece colors
            // If the identity of the piece on the destination block is not eNone, the move is legal
          }
          newY += stepFactorY;
          break; // ensure x and y step together
        }
      }
      // The for-loop ran it's course, and the return wasn't called.  I call that a success
      return true;
    }
    else {
      return true;
    }
  }
  else {
    return false;
  }
}

boardStateMapType& Board::stagingBoardStateMap()
{
  return _stagingBoardStateMap;
}

void Board::setStagingBoardStateMap(const boardStateMapType& stagingBoardStateMap)
{
  _stagingBoardStateMap = stagingBoardStateMap;
}

boardStateMapType& Board::backedUpBoardStateMap()
{
  return _backedUpBoardStateMap;
}

void Board::setBackedUpBoardStateMap(const boardStateMapType& backedUpBoardStateMap)
{
  _backedUpBoardStateMap = backedUpBoardStateMap;
}

boardStateMapType& Board::workingBoardStateMap()
{
  return _workingBoardStateMap;
}

void Board::setWorkingBoardStateMap(const boardStateMapType& workingBoardStateMap)
{
  _workingBoardStateMap = workingBoardStateMap;
}

piecesSetType& Board::stagingCapturedPieces()
{
  return _stagingCapturedPieces;
}

void Board::setStagingCapturedPieces(const piecesSetType& stagingCapturedPieces)
{
  _stagingCapturedPieces = stagingCapturedPieces;
}

piecesSetType& Board::backedUpCapturedPieces()
{
  return _backedUpCapturedPieces;
}

void Board::setBackedUpCapturedPieces(const piecesSetType& backedUpCapturedPieces)
{
  _backedUpCapturedPieces = backedUpCapturedPieces;
}

piecesSetType& Board::workingCapturedPieces()
{
  return _workingCapturedPieces;
}

void Board::setWorkingCapturedPieces(const piecesSetType& workingCapturedPieces)
{
  _workingCapturedPieces = workingCapturedPieces;
}


void Board::resetBoard(bool styleOnly)
{
  resetBoard(false, styleOnly);
}

void Board::uncheckAllCheckedCells()
{
  for (int row = 1; row <= 8; ++row)
    for (int column = 1; column <= 8; ++column) {

      Cell* cell = getCell(row, column);

      if (cell != nullptr) {
        if (cell->isChecked()) {
          cell->blockSignals(true);
          cell->setChecked(false);
          cell->blockSignals(false);
        }
      }
    }
  Cell::resetCheckedCounter();
}

void Board::redrawBoardFromMap(boardStateMapType currentBoardStateMap)
{
  // First, clear the board
  for (int row = eMinRow; row <= eMaxRow; ++row) {
    for (int column = eMinColumn; column <= eMaxColumn; ++column) {
      getCell(row, column)->clearAssignedPiece();
//      getCell(row, column)->highLightCell(false);
    }
  }

  boardStateMapType::iterator i = currentBoardStateMap.begin();

  while (i != currentBoardStateMap.end()) {

    boardCoordinateType coordinate = i.key();
    definedPieceType piece = i.value();

    ++i;
    Cell* cell = getCell(coordinate.first, coordinate.second);

    QSharedPointer<Piece> pieceInstance = QSharedPointer<Piece>(new Piece(piece.first, piece.second));
    cell->assignPiece(pieceInstance);

  }
}

void Board::resetBoard(bool forTheFirstTime, bool styleOnly)
{
  //! Create a startup map for new games
  createStartupMap(_backedUpBoardStateMap);
  createStartupMap(_workingBoardStateMap);
  createStartupMap(_stagingBoardStateMap);

  //! Initialize the board.
  for (int row = 1; row <= 8; ++row)
    for (int column = 1; column <= 8; ++column) {

      Cell* cell = getCell(row, column);

      if (cell != nullptr) {

        if (forTheFirstTime) {
          // What to connect these cells to?
          connect(cell, SIGNAL(startingANewMove(boardCoordinateType)), this, SLOT(moveInitiated(boardCoordinateType)));
          connect(cell, SIGNAL(completingMove(boardCoordinateType)), this, SLOT(continueInitiatedMove(boardCoordinateType)));
          connect(cell, SIGNAL(nothingToDo()), this, SLOT(clearHighLights()));
          cell->setCoordinate(row, column);
        }


        if (!styleOnly) {
          initializeBoardCell(cell);
        }
      }
    }
  if (forTheFirstTime) {
    connect(this, SIGNAL(moveInitiatedComplete(QSharedPointer<Player>&)), this, SLOT(handleMoveInitiatedComplete(QSharedPointer<Player>&)));
  }
  setEnabled(true);
}

Cell* Board::getCell(int row, int column) const
{
  QGridLayout* parentLayout = ui->gridLayout;
  Cell* theRealCell = nullptr;

  QLayoutItem* theCell;
  theCell = parentLayout->itemAtPosition(row, column);

  QWidget* closerToTheRealCell;
  closerToTheRealCell = theCell->widget();

  theRealCell = dynamic_cast<Cell*>(closerToTheRealCell);
  return theRealCell;
}

Cell* Board::getCell(boardCoordinateType position) const
{
  return getCell(position.first, position.second);
}

void Board::initializeBoardCell(Cell* cell)
{
  boardCoordinateType coordinate(cell->position());
  definedPieceType piece = _workingBoardStateMap.value(coordinate);

  // This cell needs a piece
  QSharedPointer<Piece> pieceInstance = QSharedPointer<Piece>(new Piece(piece.first, piece.second));

  cell->assignPiece(pieceInstance);
}

void Board::createStartupMap(boardStateMapType& mapToInitialize)
{
  if (mapToInitialize.isEmpty()) {
    for (rowType row = eMinRow; row <= eMaxRow; ++row)
      for (columnType column = eMinColumn; column <= eMaxColumn; ++column) {
        if (row > ePawnsTopRow && row < ePawnsBottomRow) { //! No pieces on the board between rows 2 and 7
          continue;
        }

        if (row == ePawnsTopRow || row == ePawnsBottomRow) { //! This will be all pawns.  Assume player always plays with white
          Pieces::PieceColors::ePieceColors pieceColor;
          if (row == ePawnsTopRow) {
            pieceColor = Pieces::PieceColors::eBlack;
          }
          else {
            pieceColor = Pieces::PieceColors::eWhite;
          }
          mapToInitialize.insert(boardCoordinateType(row, column), definedPieceType(Pieces::Identities::ePawn, pieceColor));
        }

        if (row == eOtherTopRow || row == eOtherBottomRow) { //! This is where all the more important enemy pieces start
          Pieces::PieceColors::ePieceColors pieceColor;
          if (row == eOtherTopRow) {
            pieceColor = Pieces::PieceColors::eBlack;
          }
          else {
            pieceColor = Pieces::PieceColors::eWhite;
          }
          switch (column) {
          case eCastleLeftColumn:
          case eCastleRightColumn:
            mapToInitialize.insert(boardCoordinateType(row, column), definedPieceType(Pieces::Identities::eCastle, pieceColor));
            break;
          case eKnightLeftColumn :
          case eKnightRightColumn:
            mapToInitialize.insert(boardCoordinateType(row, column), definedPieceType(Pieces::Identities::eKnight, pieceColor));
            break;
          case eBishopLeftColumn :
          case eBishopRightColumn:
            mapToInitialize.insert(boardCoordinateType(row, column), definedPieceType(Pieces::Identities::eBishop, pieceColor));
            break;
          case eKingColumn:
            mapToInitialize.insert(boardCoordinateType(row, column), definedPieceType(Pieces::Identities::eKing,   pieceColor));
            break;
          case eQueenColumn:
            mapToInitialize.insert(boardCoordinateType(row, column), definedPieceType(Pieces::Identities::eQueen,  pieceColor));
            break;
          }
        }
      }
  }
  else {
    mapToInitialize.clear();
    createStartupMap(mapToInitialize);
  }
}