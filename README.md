# BigO complexity analyzer

microservice-based tool for anlyzing the time cpmlexity of c code. it parses 50-500 lines of code, detects loops/recursion, builds a recurrence relation if one exists, and solves for the corresponding
big-o complexity. user interacts via a web interface where they can paste in code, and the complexity/recurrence relations are displayed.

--system structure--
    the system runs via three microservices as docker containers:
        -frontend - handles user input, as well as communication between all microservices (essentially the central hub)
        -parser-c - parses c code into an ast(abstract syntax tree), which is sent back to the frontend
        -analyzer - analyzes ast (which is received from the frontend) to calculate big-o complexity, as well as solve for any recurrence relations

--operating the system--
    1. ensure docker and docker desktop are installed
    2. clone repository (https://github.com/blukebill/BigO)
    3. from the project root (/BigO/), build docker compose using:
        docker compose up --build
    4. once all services are running, open browser to http://localhost:5000

--using the system--
    1. paste c code into uppermost textbox and click "analyze"
    2. view results on bottom right textbox under "complexity analysis"

    NOTE: if code includes recursion, the analyzer will attempt to model and solve the recurrence

--troubleshooting-- 
    -if any blank output is present, ensure all docker containers are online with:
        docker ps
    -invalid code will return "O(1)" under the complexity analysis textbox, along with 0 detected loops/functions
    
--deployment--
    the included docker-compose.yml file should be executable on any system with docker.
