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
app.config['SECRET_KEY'] = 'a_super_secret_key_for_flask_forms'  # Cheie secretă necesară

db = SQLAlchemy(app)

# Variabilă globală pentru logica anti-spam (5 minute = 300 secunde)
SCAN_COOLDOWN_SECONDS = 300


# --- MODEL DEFINITIONS ---

class Person(db.Model):
    __tablename__ = 'person'
    id = db.Column(db.Integer, primary_key=True)
    uid_card = db.Column(db.String(50), unique=True, nullable=False)
    first_name = db.Column(db.String(100), nullable=False)
    last_name = db.Column(db.String(100), nullable=False)

    # Starea fizică: True = Persoana este în clădire.
    is_inside = db.Column(db.Boolean, default=False)

    # Timpul ultimei scanări (pentru a calcula cooldown-ul)
    # Folosim o dată veche ca default
    last_action_time = db.Column(db.DateTime, default=lambda: datetime(1970, 1, 1, tzinfo=timezone.utc))

    logs = db.relationship('Log', backref='person', lazy=True)

    def __repr__(self):
        return f'<Person {self.last_name}, {self.first_name}>'


class Log(db.Model):
    __tablename__ = 'log'
    id = db.Column(db.Integer, primary_key=True)
    person_id = db.Column(db.Integer, db.ForeignKey('person.id'), nullable=False)

    # Folosim UTC pentru consistență
    timestamp = db.Column(db.DateTime, default=lambda: datetime.now(timezone.utc))
    # 'IN' sau 'OUT'
    action = db.Column(db.String(3), nullable=False)

    def __repr__(self):
        return f'<Log {self.action} for Person {self.person_id} at {self.timestamp}>'


# --- UTILITY: Inițializare/Curățare DB ---
def initialize_db():
    """Creează tabelele și se asigură că toți timpii sunt corecți (cu timezone)."""
    db.create_all()

    # Asigurăm că last_action_time are timezone pe toate înregistrările
    # (Prevenim erori de comparare între datetime naive și aware)
    people = Person.query.filter(Person.last_action_time.is_(None)).all()
    if people:
        print("Avertisment: Actualizare last_action_time cu valoare implicită UTC.")
        default_time = datetime(1970, 1, 1, tzinfo=timezone.utc)
        for person in people:
            # Dacă last_action_time este None (pentru înregistrările vechi fără default)
            person.last_action_time = default_time
        db.session.commit()


# --- RUTA API: /scan/<card_uid> ---
@app.route('/scan/<string:card_uid>', methods=['GET'])
def scan_rfid(card_uid):
    person = Person.query.filter_by(uid_card=card_uid).first()
    now_utc = datetime.now(timezone.utc)

    # 1. Card neînregistrat
    if not person:
        # Cod 403: Forbidden - Card necunoscut
        return jsonify({'message': 'Card neînregistrat!'},
                       {'card_uid': card_uid}), 403

    # 2. Logica Cooldown (Anti-Spam) - Cooldown INDIVIDUAL pe fiecare persoană
    last_action_time = person.last_action_time

    # Asigurăm că timpul are informația de fus orar (important pentru comparații)
    if last_action_time.tzinfo is None:
        last_action_time = last_action_time.replace(tzinfo=timezone.utc)

    time_since_last_action = now_utc - last_action_time

    if time_since_last_action.total_seconds() < SCAN_COOLDOWN_SECONDS:
        remaining_seconds = int(SCAN_COOLDOWN_SECONDS - time_since_last_action.total_seconds())
        return jsonify({
            'message': f'Scanati prea repede! Așteptati încă {remaining_seconds} secunde!',
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
    db.session.commit()

    return jsonify({
        'message': f'Scanare OK. Actiune: {new_action}. {person.first_name} {person.last_name}',
        'status': 'SUCCESS',
        'person_id': person.id
    }), 200


# --- RUTA ADMIN: Înregistrare Persoană ---
@app.route('/register', methods=['GET', 'POST'])
def register_person():
    if request.method == 'POST':
        card_uid = request.form.get('uid_card').strip().upper()  # Curățăm și standardizăm UID
        first_name = request.form.get('first_name').strip()
        last_name = request.form.get('last_name').strip()

        if not card_uid or not first_name or not last_name:
            return render_template('register.html', error_message="Toate câmpurile sunt obligatorii."), 400

        if Person.query.filter_by(uid_card=card_uid).first():
            return render_template('register.html', error_message=f"UID card ({card_uid}) deja înregistrat!"), 400

        new_person = Person(uid_card=card_uid, first_name=first_name, last_name=last_name)
        db.session.add(new_person)
        db.session.commit()

        # Redirecționare cu mesaj de succes
        return redirect(url_for('home', message_success=f'Persoana {last_name} a fost înregistrată cu succes.'))

    return render_template('register.html')


# --- RUTA ADMIN: Status Curent (Home) ---
@app.route('/')
def home():
    # Preia toate persoanele pentru afișarea statusului IN/OUT
    people = Person.query.order_by(Person.last_name).all()

    # Preluarea mesajului de succes din redirect (dacă există)
    message_success = request.args.get('message_success')

    # Calculăm statisticile pentru Cardurile de pe Dashboard
    total_people = len(people)
    people_inside = sum(1 for p in people if p.is_inside)

    return render_template('status.html',
                           people=people,
                           total_people=total_people,
                           people_inside=people_inside,
                           message_success=message_success)


# --- RUTA ADMIN: Vizualizare Loguri ---
@app.route('/logs')
def view_logs():
    # Preia log-urile cel mai nou apărând primul
    # Folosim join() automatizat de SQLAlchemy pentru a avea acces la person.last_name etc.
    logs = Log.query.order_by(Log.timestamp.desc()).all()
    return render_template('logs.html', logs=logs)


# --- RULAREA APLICATIEI ---
if __name__ == '__main__':
    with app.app_context():
        initialize_db()
    # Porniți aplicația pe toate interfețele, ideal pentru ESP32 (schimbați la 5000 dacă nu folosiți portul default)
    # app.run(host='0.0.0.0', port=5000, debug=True)
    app.run(debug=True)