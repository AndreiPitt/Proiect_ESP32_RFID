// script.js
document.addEventListener('DOMContentLoaded', () => {
    let websocket;
    let usersData = []; 
    let lastScannedUID = ''; 
    let isAdminMode = false;
    const databaseDisplayEl = document.getElementById('database-display');

    // Inițiază conexiunea WebSocket
    function initWebSocket() {
        console.log('Trying to open a WebSocket connection...');
        const gateway = `ws://${window.location.hostname}/ws`;
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
        websocket.onerror = onError;
    }

    // Funcție pentru citirea bazei de date a utilizatorilor
    function fetchUsersData() {
        hideRegistrationForm();
        setAdminMode(false);
        hideDatabase(); 
        
        fetch('/users.json') 
            .then(response => {
                if (!response.ok) {
                    throw new Error(`HTTP error! status: ${response.status}`);
                }
                return response.text(); 
            })
            .then(text => {
                if (text.trim() === '') {
                    usersData = [];
                    console.warn('users.json este gol. Initializare lista utilizatori goala.');
                } else {
                    try {
                         usersData = JSON.parse(text); 
                    } catch (e) {
                        console.error('Eroare la parsarea JSON primit din users.json:', e);
                        usersData = [];
                    }
                }

                console.log('Utilizatori încărcați și stocați:', usersData);
                updateStatus('Conectat și Gata de Scanare!', 'connected');
            })
            .catch(error => {
                console.error('Eroare la citirea users.json:', error);
                updateStatus('Eroare la incarcarea datelor...', 'disconnected');
                setTimeout(fetchUsersData, 5000); 
            });
    }

    function onOpen(event) {
        console.log('WebSocket connection opened successfully.');
        fetchUsersData(); 
    }

    function onClose(event) {
        console.log('WebSocket connection closed. Reconnecting...');
        updateStatus('Deconectat. Reconectare în curs...', 'disconnected');
        setTimeout(initWebSocket, 2000); 
    }

    function onMessage(event) {
        try {
            const data = JSON.parse(event.data); 
            
            if (data.type === 'UID_SCAN' && data.uid) {
                hideRegistrationForm(); 
                hideDatabase(); 
                
                const uid = data.uid.toLowerCase(); 
                lastScannedUID = data.uid.toUpperCase(); 
                
                const foundUser = Array.isArray(usersData) ? usersData.find(user => user.UID.toLowerCase() === uid) : undefined;

                if (isAdminMode) {
                    // LOGICA MOD ADMIN
                    if (foundUser && foundUser.Rol === 'Admin') {
                        // Card de administrator scanat cu succes!
                        displayUserInfo(foundUser, 'Acces Admin Permis');
                        displayDatabase(usersData); 
                        setAdminMode(false); 
                    } else {
                        // Card scanat, dar nu e Admin
                        displayUserInfo({ UID: lastScannedUID, Nume: 'ACCES', Prenume: 'REFUZAT', Rol: 'N/A' }, 'Acces Admin Interzis');
                        setAdminMode(false); 
                    }
                    return; 
                }

                // LOGICA STANDARD 
                if (foundUser) {
                    console.log('Acces permis pentru:', foundUser);
                    displayUserInfo(foundUser, 'Permis');
                } else {
                    console.warn('Acces interzis. UID necunoscut:', uid);
                    displayUserInfo({ UID: lastScannedUID, Nume: 'NECUNOSCUT', Prenume: 'N/A', Rol: 'N/A' }, 'Înregistrare Necesară');
                    showRegistrationForm(lastScannedUID); 
                }
            } else if (data.type === 'REGISTER_SUCCESS') {
                alert('Utilizator înregistrat cu succes!');
                fetchUsersData(); 
                displayUserInfo(data.user, 'Permis (Nou)'); 
            }

        } catch (e) {
            console.error('Eroare la parsarea JSON primit:', e);
        }
    }

    function onError(event) {
        console.error('WebSocket Error:', event);
    }
    
    // Funcție pentru salvarea utilizatorului
    function saveNewUser(event) {
        event.preventDefault(); 

        const nume = document.getElementById('reg-nume').value.trim();
        const prenume = document.getElementById('reg-prenume').value.trim();
        const rol = document.getElementById('reg-rol').value;

        if (!nume || !prenume || !rol) {
            alert('Vă rugăm completați toate câmpurile!');
            return;
        }
        if (rol === 'Admin') {
             alert('Rolul de Administrator nu poate fi setat prin formularul de înregistrare.');
             return;
        }

        const registrationMessage = {
            type: 'REGISTER',
            uid: lastScannedUID, 
            Nume: nume,
            Prenume: prenume,
            Rol: rol
        };
        
        if (websocket && websocket.readyState === websocket.OPEN) {
            websocket.send(JSON.stringify(registrationMessage));
            console.log("Trimis cerere de înregistrare:", registrationMessage);
            document.getElementById('user-display').innerHTML = '<p>Se trimite cererea de înregistrare...</p>';
        } else {
            alert('Eroare: Conexiunea WebSocket este închisă. Vă rugăm reîncărcați pagina.');
        }
    }

    // Funcție NOUĂ: Afișează lista de utilizatori
    function displayDatabase(users) {
        if (!databaseDisplayEl) return;
        
        let htmlContent = '<h4>Baza de Date Utilizatori</h4>';

        if (!Array.isArray(users) || users.length === 0) {
            htmlContent += '<p class="warning">Baza de date este goală.</p>';
        } else {
            htmlContent += '<table>';
            htmlContent += '<tr><th>UID</th><th>Nume Prenume</th><th>Rol</th></tr>';
            
            users.forEach(user => {
                htmlContent += `
                    <tr>
                        <td>${user.UID.toUpperCase()}</td>
                        <td>${user.Nume} ${user.Prenume}</td>
                        <td>${user.Rol}</td>
                    </tr>
                `;
            });
            
            htmlContent += '</table>';
        }
        
        databaseDisplayEl.innerHTML = htmlContent;
        databaseDisplayEl.style.display = 'block';
    }

    function hideDatabase() {
        if (databaseDisplayEl) {
            databaseDisplayEl.style.display = 'none';
            databaseDisplayEl.innerHTML = '';
        }
    }


    // Funcție Gestiunea modului Admin
    function setAdminMode(state) {
        isAdminMode = state;
        const adminStatusEl = document.getElementById('admin-status');
        const adminButtonEl = document.getElementById('admin-button');
        
        if (state) {
            adminStatusEl.textContent = 'Mod Administrare ACTIV! Scanați cardul Admin...';
            adminStatusEl.classList.add('active-admin');
            adminButtonEl.disabled = true;
            displayUserInfo({ UID: 'N/A', Nume: 'MOD', Prenume: 'ADMIN', Rol: 'ACTIV' }, 'Așteaptă Card');
        } else {
            adminStatusEl.textContent = '';
            adminStatusEl.classList.remove('active-admin');
            adminButtonEl.disabled = false;
        }
    }

    // Funcții Utilitare pentru UI
    function updateStatus(message, className) {
        const statusEl = document.getElementById('status');
        if (statusEl) {
            statusEl.textContent = message;
            statusEl.className = 'status-' + className;
        }
    }
    
    function displayUserInfo(user, accessStatus) {
        const displayEl = document.getElementById('user-display');
        const accessClass = accessStatus.includes('Permis') || accessStatus.includes('Așteaptă') ? 'access-granted' : 'access-denied';

        if (displayEl) {
            displayEl.innerHTML = `
                <p class="${accessClass}"><strong>Acces: ${accessStatus}</strong></p>
                <p><strong>UID Scanat:</strong> ${user.UID.toUpperCase()}</p>
                <p><strong>Nume:</strong> ${user.Nume} <strong>Prenume:</strong> ${user.Prenume}</p>
                <p><strong>Rol:</strong> ${user.Rol}</p>
            `;
        }
    }

    function showRegistrationForm(uid) {
        const formDiv = document.getElementById('registration-form');
        document.getElementById('reg-uid-display').textContent = uid;
        if (formDiv) {
            formDiv.style.display = 'block';
        }
    }

    function hideRegistrationForm() {
        const formDiv = document.getElementById('registration-form');
        if (formDiv) {
            formDiv.style.display = 'none';
            document.getElementById('registration-form-element').reset();
            document.getElementById('reg-uid-display').textContent = '';
        }
    }
    
    // Adaugare listener pe butonul de administrare
    document.getElementById('admin-button').addEventListener('click', () => {
        hideRegistrationForm();
        hideDatabase(); 
        setAdminMode(true);
    });
    
    window.saveNewUser = saveNewUser;
    
    initWebSocket(); 
});