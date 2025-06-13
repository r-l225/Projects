#include <stdio.h>
#include <stdlib.h>
#define HAVE_STRUCT_TIMESPEC
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

// Constants for game configuration
#define NUM_CARDS 52
#define MAX_PLAYERS 10
#define HAND_SIZE 2

// Structure for a playing card
typedef struct {
    int value;
    char suit;
} Card;

// Structure for a player
typedef struct {
    int id;
    Card hand[HAND_SIZE];
    pthread_t thread;
    int hand_size;
} Player;

// Main game state structure
typedef struct {
    Card deck[NUM_CARDS];
    Card discarded[NUM_CARDS];
    int numDiscarded;
    int deck_size;
    Card greasyCard;
    int currentPlayer;
    int dealerId;
    bool isBagOpen;
    int round;
    int totalRounds;
    pthread_mutex_t deck_mutex;
    pthread_mutex_t turn_mutex;
    pthread_cond_t turn_cond;
    FILE *log_file;
    pthread_mutex_t log_mutex;
    pthread_mutex_t chip_mutex;
    int numPlayers;
    int numChips;
    int chips_in_bag;
    int total_bags_used;
    Player players[];
} GameState;

// Global game state variable
GameState game;
Player players[MAX_PLAYERS];

// Function Prototypes
void shuffleDeck(Card deck[]);
void openNewBag(GameState *game);
Card drawCard(GameState *game);
void* playerRoutine(void *arg);
void dealCards(GameState *game);
void initializeGame(GameState *game, int numPlayers, int numChips);
void initializeDeck(Card deck[]);
void cleanup(GameState *game);
void logAction(const char* action);
void cleanup(GameState *game);
void* playerRoutine(void* arg);
void waitForTurn(GameState *game, int playerId);
void signalNextPlayerTurn(GameState* game, int nextPlayerId);
void declareWinner(GameState *game, Player* player);
void declareLosers(GameState* game, int winnerId);
void endRound(GameState *game);
void startRound(GameState *game);
void discardCard(GameState *game, Player* player, Card card);
void eatChips(GameState *game, Player *player);
void logDeckContents(GameState* game);
void displayPlayerHand(FILE *log_file, Player *player);
const char* cardValueStr(int value);

void displayPlayerHand(FILE *log_file, Player *player) {
    fprintf(log_file, "PLAYER %d: hand ", player->id);
    if (player->hand_size > 0) {
        for (int i = 0; i < player->hand_size; i++) {
            // Print each card in the player's hand
            fprintf(log_file, "%s", cardValueStr(player->hand[i].value));
            // Print a comma after each card except the last
            if (i < player->hand_size - 1) {
                fprintf(log_file, ",");
            }
        }
    }
    // Move to the next line after printing the hand
    fprintf(log_file, "\n");
}
void logDeckContents(GameState* game) {
    char log_message[1024] = "DECK: "; // Start the log message with "DECK: "

    for (int i = 0; i < game->deck_size; ++i) {
        char cardStr[5]; // Buffer to hold the string representation of each card
        // Convert the card value to a string representation (A, J, Q, K, or number)
        const char* cardValueString = cardValueStr(game->deck[i].value);
        sprintf(cardStr, "%s ", cardValueString); // Format the card string
        strcat(log_message, cardStr); // Append the card string to the log message
    }

    logAction(log_message); // Log the deck contents
}

const char* cardValueStr(int value) {
    static char numStr[3]; // Static buffer to hold string representation of the number

    switch (value) {
        case 1: return "A"; // Return "A" for Ace
        case 11: return "J"; // Return "J" for Jack
        case 12: return "Q"; // Return "Q" for Queen
        case 13: return "K"; // Return "K" for King
        default:
            sprintf(numStr, "%d", value); // Convert numeric value to string for other cards
            return numStr;
    }
}

void signalNextPlayerTurn(GameState* game, int nextPlayerId) {
    pthread_mutex_lock(&game->turn_mutex); // Lock the turn mutex to ensure thread-safe access

    // Set the current player to the next player's ID
    game->currentPlayer = nextPlayerId;

    // Signal to all waiting threads that the turn has changed.
    // pthread_cond_broadcast is used here to wake up all threads waiting on this condition variable.
    // Each thread will then check if it is their turn (based on currentPlayer) before taking action.
    pthread_cond_broadcast(&game->turn_cond);

    pthread_mutex_unlock(&game->turn_mutex); // Unlock the turn mutex
}

void waitForTurn(GameState *game, int playerId) {
    pthread_mutex_lock(&game->turn_mutex); // Lock the turn mutex to ensure thread-safe access to the shared game state

    // Continuously check if it's the player's turn.
    // The while loop is used instead of an if statement to handle spurious wake-ups.
    while (game->currentPlayer != playerId) {
        // If it's not this player's turn, wait on the turn condition variable.
        // pthread_cond_wait atomically unlocks the mutex and waits for the condition variable to be signaled.
        // When pthread_cond_wait returns (after being signaled), the mutex is automatically re-locked.
        pthread_cond_wait(&game->turn_cond, &game->turn_mutex);
    }

    pthread_mutex_unlock(&game->turn_mutex); // Unlock the turn mutex
}

void logAction(const char* action) {
    pthread_mutex_lock(&game.log_mutex); // Lock the log mutex to ensure thread-safe access to the log file

    // Check if the log file is open
    if (game.log_file) {
        // Write the action string to the log file
        fprintf(game.log_file, "%s\n", action);

        // Flush the output buffer to ensure that the action is written to the file immediately
        // This is important in a multithreaded environment to ensure logs are written in real-time
        fflush(game.log_file);
    }

    pthread_mutex_unlock(&game.log_mutex); // Unlock the log mutex
}

void shuffleDeck(Card deck[]) {
    // Loop through the deck from the end to the beginning
    for (int i = NUM_CARDS - 1; i > 0; --i) {
        // Generate a random number between 0 and i (inclusive)
        int j = rand() % (i + 1);

        // Swap the card at index i with the card at the randomly chosen index j
        Card temp = deck[i];  // Store the current card in a temporary variable
        deck[i] = deck[j];    // Move the card at index j to index i
        deck[j] = temp;       // Move the card from the temporary variable to index j
    }
}
void dealCards(GameState *game) {
    // Loop through all the players in the game
    for (int i = 0; i < game->numPlayers; ++i) {
        // Draw a card from the deck and assign it to the player's hand
        // Increment the player's hand size after assigning the card
        game->players[i].hand[game->players[i].hand_size++] = drawCard(game);

        // You can log the action of dealing a card to each player if desired
        // For example: Log the player ID and the card they received
    }
}
Card drawCard(GameState* game) {
    // Lock the mutex to ensure thread-safe access to the deck
    pthread_mutex_lock(&game->deck_mutex);

    // Check if the deck is empty
    if (game->deck_size == 0) {
        // Unlock the mutex before exiting the function
        pthread_mutex_unlock(&game->deck_mutex);

        // Handle the empty deck situation
        fprintf(stderr, "The deck is empty, cannot draw a card.\n");
        // Terminate the program if the deck is empty - you might want to handle this differently
        exit(EXIT_FAILURE);
    }

    // Draw the top card from the deck
    Card drawnCard = game->deck[game->deck_size - 1];
    // Decrement the deck size to indicate that a card has been drawn
    game->deck_size--;

    // Unlock the mutex after accessing the deck
    pthread_mutex_unlock(&game->deck_mutex);

    // Return the drawn card
    return drawnCard;
}

void initializeGame(GameState *game, int numPlayers, int numChips) {
    // Reset the game state to zero. This clears all values, ensuring a clean start.
    memset(game, 0, sizeof(GameState));

    // Initialize the deck of cards
    initializeDeck(game->deck);
    game->deck_size = NUM_CARDS; // Set the initial deck size

    // Set basic game parameters
    game->numPlayers = numPlayers; // Number of players in the game
    game->totalRounds = numPlayers; // Assuming a round for each player
    game->chips_in_bag = numChips; // Number of chips in the bag
    game->numChips = numChips;     // Total number of chips
    game->currentPlayer = 1;       // Start with player 1
    game->dealerId = game->currentPlayer; // The first dealer is the first player
    game->round = 1; // Start at round 1
    game->total_bags_used = 1; // Start with the first bag of chips

    // Open the log file for recording game actions
    game->log_file = fopen("game_log.txt", "w");
    if (!game->log_file) {
        // Handle file open errors
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    // Initialize mutexes and condition variables
    pthread_mutex_init(&game->deck_mutex, NULL); // Mutex for deck access
    pthread_mutex_init(&game->turn_mutex, NULL); // Mutex for turn control
    pthread_cond_init(&game->turn_cond, NULL);   // Condition variable for turns
    pthread_mutex_init(&game->log_mutex, NULL);  // Mutex for logging actions
    pthread_mutex_init(&game->chip_mutex, NULL); // Mutex for chip bag access

    // Initialize player data
    for (int i = 0; i < numPlayers; i++) {
        game->players[i].id = i + 1; // Assign player IDs starting from 1
        game->players[i].hand_size = 0; // Initialize each player's hand size to 0
    }
}

void initializeDeck(Card deck[NUM_CARDS]) {
    // Define the four suits of a standard card deck
    const char suits[] = {'H', 'D', 'C', 'S'}; // Hearts, Diamonds, Clubs, Spades

    int index = 0; // Initialize index to track position in the deck array

    // Loop through each suit
    for (int suit = 0; suit < 4; ++suit) {
        // Loop through each card value (1 to 13, corresponding to Ace, 2-10, Jack, Queen, King)
        for (int value = 1; value <= 13; ++value) {
            deck[index].suit = suits[suit]; // Assign the suit to the current card
            deck[index].value = value;      // Assign the value to the current card
            index++; // Move to the next position in the deck array
        }
    }
    // After the loop, the deck array is filled with 52 cards, in order of suits and values.
}

void cleanup(GameState *game) {
    // Check if the log file is open
    if (game->log_file) {
        fclose(game->log_file); // Close the log file
    }

    // Destroy the mutexes
    pthread_mutex_destroy(&game->deck_mutex); // Destroys the mutex for deck access
    pthread_mutex_destroy(&game->log_mutex);  // Destroys the mutex for log file access
    pthread_mutex_destroy(&game->turn_mutex); // Destroys the mutex for turn control

    // Destroy the condition variable
    pthread_cond_destroy(&game->turn_cond);   // Destroys the condition variable for turn signaling

    // You can add more cleanup code here if there are other resources to release
}

void discardCard(GameState *game, Player* player, Card card) {
    // Lock the deck mutex to ensure exclusive access to the deck
    pthread_mutex_lock(&game->deck_mutex);

    // Add the discarded card to the bottom of the deck
    for (int i = game->deck_size; i > 0; i--) {
        game->deck[i] = game->deck[i - 1]; // Shift all cards up by one position
    }
    game->deck[0] = card; // Place the discarded card at the bottom of the deck
    game->deck_size++; // Increment the deck size to account for the new card

    // Unlock the deck mutex after modifying the deck
    pthread_mutex_unlock(&game->deck_mutex);
}

void declareWinner(GameState* game, Player* winner) {
    // Create a log message stating the winner of the round
    char log_message[128];
    sprintf(log_message, "PLAYER %d: wins round %d", winner->id, game->round);

    // Log this action by calling logAction
    logAction(log_message);

    // Call declareLosers to log the other players as having lost the round
    declareLosers(game, winner->id);
}

void declareLosers(GameState* game, int winnerId) {
    char log_message[128];

    // Iterate through all players in the game
    for (int i = 0; i < game->numPlayers; i++) {
        // Check if the current player is not the winner
        if (game->players[i].id != winnerId) {
            // Create a log message for each player who did not win
            sprintf(log_message, "PLAYER %d: lost round %d", game->players[i].id, game->round);

            // Log the message using the logAction function
            logAction(log_message);
        }
    }
}

void openNewBag(GameState *game) {
    // Reset the number of chips in the bag to the initial count
    game->chips_in_bag = game->numChips;

    // Increment the count of total bags used in the game
    game->total_bags_used++;

    // Log the event of opening a new bag of chips and the current number of chips in it
    fprintf(game->log_file, "New bag of chips opened\nBAG: %d Chips left\n", game->chips_in_bag);
}
void eatChips(GameState *game, Player *player) {
    // Lock the mutex to ensure exclusive access to the chips
    pthread_mutex_lock(&game->chip_mutex);

    // Randomly determine the number of chips to eat, between 1 and 5
    int chips_to_eat = (rand() % 5) + 1;

    // If the bag is empty, open a new bag of chips
    if (game->chips_in_bag <= 0) {
        openNewBag(game);
    }

    // If the number of chips to eat is more than what's left in the bag, adjust it
    if (chips_to_eat > game->chips_in_bag) {
        chips_to_eat = game->chips_in_bag;
    }

    // Subtract the eaten chips from the bag
    game->chips_in_bag -= chips_to_eat;

    // Log the action of the player eating chips and the remaining chips in the bag
    fprintf(game->log_file, "PLAYER %d: eats %d chips\n", player->id, chips_to_eat);
    fprintf(game->log_file, "BAG: %d Chips left\n", game->chips_in_bag);

    // Unlock the mutex
    pthread_mutex_unlock(&game->chip_mutex);
}

void startRound(GameState *game) {
    // Log the start of the round with the current dealer's ID
    fprintf(game->log_file, "Player %d: Round starts\n", game->dealerId);
    fflush(game->log_file);  // Ensure the log is updated immediately

    // Shuffle the deck to randomize the card order
    shuffleDeck(game->deck);
    game->deck_size = NUM_CARDS; // Reset the deck size back to full

    // Draw a card to determine the "Greasy Card" for this round
    game->greasyCard = drawCard(game);
    // Log the drawn "Greasy Card" using its string representation (A, J, Q, K for 1, 11, 12, 13)
    fprintf(game->log_file, "Player %d: draws Greasy card %s\n",
            game->dealerId, cardValueStr(game->greasyCard.value));

    // Deal one new card to each player
    for (int i = 0; i < game->numPlayers; ++i) {
        game->players[i].hand_size = 0; // Reset each player's hand size to 0

        // Draw a new card for the player
        Card newCard = drawCard(game);
        // Add the drawn card to the player's hand
        game->players[i].hand[game->players[i].hand_size++] = newCard;

        // Log the card that was drawn for the player
        fprintf(game->log_file, "PLAYER %d: draws %s\n",
                game->players[i].id, cardValueStr(newCard.value));
    }

    // Flush the log file to ensure all information is written
    fflush(game->log_file);
}

void endRound(GameState *game) {
    // Log the end of the round with the current dealer's ID
    fprintf(game->log_file, "Player %d: Round ends\n", game->dealerId);
    fprintf(game->log_file, "\n"); // Adding a newline for better readability in the log

    // Update the dealer for the next round by cycling to the next player
    game->dealerId = (game->dealerId % game->numPlayers) + 1;

    // Increment the round number
    game->round++;

    // Check if there are more rounds to play
    if (game->round <= game->totalRounds) {
        // If more rounds are left, start the next round
        startRound(game);
    } else {
        // If all rounds have been played, log the game completion and perform cleanup
        fprintf(game->log_file, "Game completed after %d rounds.\n", game->totalRounds);
        fflush(game->log_file); // Ensure the log file is updated
        cleanup(game); // Clean up resources used by the game
        printf("Game has ended. Thank you for playing!\n"); // Print a message to the console
    }
}
void* playerRoutine(void* arg) {
    Player* player = (Player*)arg; // Cast the argument to a Player structure

    // Continue playing until all rounds are completed
    while (game.round <= game.totalRounds) {
        // Wait for the player's turn
        waitForTurn(&game, player->id);

        // Draw a card if the player has less than 2 cards
        if (player->hand_size < HAND_SIZE) {
            Card drawnCard = drawCard(&game);
            player->hand[player->hand_size++] = drawnCard;
            const char* cardStr = cardValueStr(drawnCard.value);
            // Log the drawn card
            fprintf(game.log_file, "PLAYER %d: draws %s\n", player->id, cardStr);
        }

        // Check if player's hand contains the Greasy card
        bool hasGreasyCard = false;
        for (int i = 0; i < player->hand_size; i++) {
            if (player->hand[i].value == game.greasyCard.value) {
                hasGreasyCard = true;
                break;
            }
        }

        // Log the player's hand if it contains the Greasy card
        if (hasGreasyCard) {
            displayPlayerHand(game.log_file, player);
            fprintf(game.log_file, " <> Greasy card is %s\n", cardValueStr(game.greasyCard.value));
        }

        // Discard a card if the player doesn't have the Greasy card and hand is full
        if (!hasGreasyCard && player->hand_size == HAND_SIZE) {
            int randomIndex = rand() % player->hand_size;
            Card cardToDiscard = player->hand[randomIndex];
            // Remove the discarded card from hand
            for (int i = randomIndex; i < player->hand_size - 1; i++) {
                player->hand[i] = player->hand[i + 1];
            }
            player->hand_size--;
            // Log the discarded card
            fprintf(game.log_file, "PLAYER %d: discards %s at random\n", player->id, cardValueStr(cardToDiscard.value));

            // Discard the card and update game state
            discardCard(&game, player, cardToDiscard);
            displayPlayerHand(game.log_file, player);
            logDeckContents(&game);
            eatChips(&game, player);
        } else if (hasGreasyCard) {
            // Declare the player as the winner if they have the Greasy card
            declareWinner(&game, player);
            endRound(&game);
        }

        // Signal the next player's turn
        signalNextPlayerTurn(&game, (player->id % game.numPlayers) + 1);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // Check for correct number of command-line arguments
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <seed> <num_players> <chips_per_bag>\n", argv[0]);
        return 1;
    }

    // Parse command-line arguments
    int seed = atoi(argv[1]);
    int numPlayers = atoi(argv[2]);
    int numChips = atoi(argv[3]);

    // Seed the random number generator for card shuffling
    srand(seed);

    // Initialize the game state with the given parameters
    initializeGame(&game, numPlayers, numChips);

    // Main game loop to go through all the rounds
    for (int round = 1; round <= game.totalRounds; round++) {
        // Start a new round
        startRound(&game);

        // Create threads for each player, simulating their actions
        for (int i = 0; i < game.numPlayers; i++) {
            players[i].id = i + 1;
            int ret = pthread_create(&players[i].thread, NULL, playerRoutine, (void*)&players[i]);
            if (ret != 0) {
                perror("Failed to create the player thread");
                cleanup(&game);
                return 1;
            }
        }

        // Wait for all player threads to finish before proceeding
        for (int i = 0; i < game.numPlayers; i++) {
            if (pthread_join(players[i].thread, NULL) != 0) {
                perror("Failed to join the player thread");
            }
        }

        // End the current round and prepare for the next
        endRound(&game);

        // Update the dealer for the next round
        game.dealerId = (game.dealerId % game.numPlayers) + 1;
    }

    // Clean up resources after the game ends
    cleanup(&game);

    return 0;
}
