# Arbitration Setup/Usage

The arbitration contract is a tool for Telos Blockchain Network members and Arbitrators to contractually file for and process arbitration within the network.

## Setup

The following steps outline how to initialize the arbitration contract and elect arbitrators.

### Contract Initialization

* `void setconfig(uint16_t max_elected_arbs, uint32_t election_duration, uint32_t start_election, uint32_t arbitrator_term_length, vector<int64_t> fees)`

    The setconfig action initializes the contract config with the given values.

    `max_elected_arbs` is the max number of arbs allowed to be elected at any given time.

    `election_duration` is the length of arbitrator elections in seconds.

    `start_election` is the length of time in seconds to wait before an election begins.

    `arbitrator_term_length` is the term length of arbitrators in seconds.

    `fees` is the fee structure for arbitration cases.

### Arbitrator Applications

* `regcand(name candidate, string creds_ipfs_url)`

    The regcand action will create an arbitrator candidate application for the given candidate. Arbitrator candidates must then be elected through the regular arbitrator election process in order to begin accepting cases.

    Note that this action only submits a candidate's application for review. In order to be *elected* as an arbitrator, the candidate must be explicitly added to an election through the `candaddlead` action.

    `candidate` is the account name of the candidate applying for an arbitrator position.

    `creds_ipfs_url` is a link (ideally an IPFS hash) to the candidate's credentials. Candidates that fail to supply or supply false credentials will not be considered for election.

* `unregcand(name candidate)`

    The unregcand action will remove the candidate's arbitrator application.

    `candidate` is the account to remove from the candidate's list.

### Arbitrator Election Setup/Management

* `initelection()`

    The initelection action will register an election by sending an inline action to the `regballot()` action on Trail. This will initialize the election with the proper values needed to hold an arbitrator election.

* `candaddlead(name candidate, string creds_ipfs_url)`

    The candaddlead action will add a single candidate to an election. Note that candidates can only be added to an election **before** voting begins.

    `candidate` is the name of the account being added.

    `creds_ipfs_url` is the url link to the candidate's credentials.

* `candrmvlead(name candidate)`

    The candrmvlead action will remove a single candidate from an existing election. Note that candidates can only be removed from an election **before** voting begins.

    `candidate` is the name of the account to remove.

* `endelection(name candidate)`

    The endelection action will close an existing election that is no longer open for voting.

    `candidate` is the account calling the endelection action.

## Filing For Arbitration

Description here...

### Common Terminology

* `Claimant` : 

* `Respondant` :

* `Arbitrator` : 

### Case Lifecycle

* `CASE_SETUP` : When an arbitration case is filed it begins in the CASE_SETUP stage where the claimant can build the case file before opening it up for review by arbitrators.

* `AWAITING_ARBS` : After a case file has been built and is ready for review by an arbitrator, the case enters the AWAITING_ARBS stage where it awaits for abritrators to be assigned.

* `CASE_INVESTIGATION` : When a case has been assigned one or more arbitrators, the case then enters the CASE_INVESTIGATION stage. In this stage the arbitrators review and assess the claims within the case to determine their validity. This stage may also involve communication between the claimant and the arbitrator in situations that require clarification or more sufficient evidence.

* `RESOLVED` : 

* `DISMISSED` : If during the CASE_INVESTIGATION stage the case is found to be invalid or without merit the arbitrator may move to file the case as DISMISSED.

