from flask import Flask, request, jsonify, render_template, redirect, url_for
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime, timezone
import os

# --- FLASK AND DB SETUP ---
app = Flask(__name__)

# Configurare calea bazei de date (folosind calea absolută pentru siguranță)
basedir = os.path.abspath(os.path.dirname(__file__))
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///' + os.path.join(basedir, 'rfid_app.db')
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
app.config['SECRET_KEY'] = 'a_super_secret_key_for_flask_forms'  # Adăugăm o cheie secretă

db = SQLAlchemy(app)  # Inițializarea bazei de date SQLAlchemy

# Variabilă globală pentru logica anti-spam (5 minute)
SCAN_COOLDOWN_SECONDS = 300


# --- MODEL DEFINITIONS (Mutate aici pentru a evita ImportError) ---

class Person(db.Model):
    __tablename__ = 'person'
    id = db.Column(db.Integer, primary_key=True)
    # UID-ul cardului (cheie unică)
    uid_card = db.Column(db.String(50), unique=True, nullable=False)
    first_name = db.Column(db.String(100), nullable=False)
    last_name = db.Column(db.String(100), nullable=False)

    # Starea fizică: Când e True = Persoana este în clădire.
    is_inside = db.Column(db.Boolean, default=False)

    # Timpul ultimei scanări (pentru a calcula cooldown-ul)
    # Folosim datetime.min (sau o dată veche) pentru prima scanare
    last_action_time = db.Column(db.DateTime, default=datetime(1970, 1, 1, tzinfo=timezone.utc))

    logs = db.relationship('Log', backref='person', lazy=True)

    def __repr__(self):
        return f'<Person {self.last_name}, {self.first_name}>'


class Log(db.Model):
    __tablename__ = 'log'
    id = db.Column(db.Integer, primary_key=True)
    # Legătura către persoana care a scanat
    person_id = db.Column(db.Integer, db.ForeignKey('person.id'), nullable=False)

    # Folosim UTC pentru consistență
    timestamp = db.Column(db.DateTime, default=lambda: datetime.now(timezone.utc))
    # 'IN' sau 'OUT'
    action = db.Column(db.String(3), nullable=False)

    def __repr__(self):
        return f'<Log {self.action} for Person {self.person_id} at {self.timestamp}>'


# --- RUTA API: /scan/<card_uid> ---
# Metoda GET este folosită pentru simplitate în comunicarea cu ESP32
@app.route('/scan/<string:card_uid>', methods=['GET'])
def scan_rfid(card_uid):
    person = Person.query.filter_by(uid_card=card_uid).first()
    now_utc = datetime.now(timezone.utc)

    # 1. Card neînregistrat
    if not person:
        # Cod 403: Forbidden - Card necunoscut
        return jsonify({'message': 'Card neînregistrat!'}), 403

    # 2. Logica Cooldown (Anti-Spam)
    # Asigurăm că last_action_time are timezone information, dacă nu are, o presupunem UTC
    last_action_time = person.last_action_time
    if last_action_time.tzinfo is None:
        # Dacă este naive, presupunem că este UTC (sau echivalentul lui min datetime)
        last_action_time = last_action_time.replace(tzinfo=timezone.utc)

    time_since_last_action = now_utc - last_action_time

    if time_since_last_action.total_seconds() < SCAN_COOLDOWN_SECONDS:
        # Cod 429: Too Many Requests
        return jsonify({
            'message': 'Scanați prea repede! Așteptați.',
            'status': 'COOLDOWN'
        }), 429

    # 3. Determinare Acțiune și Update Stare
    if person.is_inside:
        new_action = 'OUT'
        person.is_inside = False
    else:
        new_action = 'IN'
        person.is_inside = True

    # 4. Salvare în Baza de Date
    person.last_action_time = now_utc  # Actualizăm timpul ultimei acțiuni (UTC)
    new_log = Log(person_id=person.id, action=new_action)

    db.session.add(new_log)
    db.session.commit()  # Salvează TOATE modificările

    return jsonify({
        'message': f'Scanare OK. Actiune: {new_action}. {person.first_name} {person.last_name}',
        'status': 'SUCCESS',
        'person_id': person.id
    }), 200


# --- RUTA ADMIN: Înregistrare Persoană ---
@app.route('/register', methods=['GET', 'POST'])
def register_person():
    if request.method == 'POST':
        # Procesează formularul
        card_uid = request.form.get('uid_card')
        first_name = request.form.get('first_name')
        last_name = request.form.get('last_name')

        if not card_uid or not first_name or not last_name:
            return render_template('register.html', error_message="Toate câmpurile sunt obligatorii."), 400

        if Person.query.filter_by(uid_card=card_uid).first():
            # Eroare: cardul există deja
            return render_template('register.html', error_message="UID card deja înregistrat!"), 400

        new_person = Person(uid_card=card_uid, first_name=first_name, last_name=last_name)
        db.session.add(new_person)
        db.session.commit()
        return redirect(url_for('home'))

    return render_template('register.html')  # Afișează formularul


# --- RUTA ADMIN: Status Curent (Home) ---
@app.route('/')
def home():
    # Preia toate persoanele pentru afișarea statusului IN/OUT
    people = Person.query.order_by(Person.last_name).all()
    return render_template('status.html', people=people)


# --- RUTA ADMIN: Vizualizare Loguri ---
@app.route('/logs')
def view_logs():
    # Preia log-urile cel mai nou apărând primul
    # Folosim .options(db.joinedload(Log.person)) pentru a prelua numele persoanei eficient (nu este necesar load-ul explicit cu SQLAlchemy 2.0+)
    logs = Log.query.order_by(Log.timestamp.desc()).all()
    return render_template('logs.html', logs=logs)


# --- RULAREA APLICATIEI ---
if __name__ == '__main__':
    # Această secțiune este rulată doar când pornești scriptul direct
    with app.app_context():
        # Creează tabelele Person și Log în baza de date
        db.create_all()
    app.run(debug=True)