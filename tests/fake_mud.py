#!/usr/bin/env python3
"""Tiny fake MUD for manual/integration testing: GMCP, MCCP2, EOR, ECHO (password), ANSI, a 5-room map."""
import socket, threading, zlib, json, sys, time
IAC,DONT,DO,WONT,WILL,SB,SE,EOR=255,254,253,252,251,250,240,239
NOEOR='--noeor' in sys.argv   # python3 fake_mud.py 4999 --noeor
ROOMS={ '1':('Temple of Dawn','Town',{'n':'2','e':'3'}), '2':('Market Square','Town',{'s':'1','n':'4'}),
        '3':('Cozy Inn','Town',{'w':'1','u':'5'}), '4':('North Gate','Town',{'s':'2'}), '5':('Dusty Attic','Town',{'d':'3'})}
class Client(threading.Thread):
    def __init__(s,c): super().__init__(daemon=True); s.c=c; s.z=None; s.room='1'; s.hp=100
    def send(s,b):
        if isinstance(b,str): b=b.encode()
        if s.z: b=s.z.compress(b)+s.z.flush(zlib.Z_SYNC_FLUSH)
        s.c.sendall(b)
    def gmcp(s,pkg,obj): s.send(bytes([IAC,SB,201])+(pkg+' '+json.dumps(obj)).encode()+bytes([IAC,SE]))
    def prompt(s,t): s.send(t.encode() if NOEOR else t.encode()+bytes([IAC,EOR]))   # NOEOR: CircleMUD style, prompt is just text with no newline
    def show(s):
        n,a,ex=ROOMS[s.room]
        s.send(f"\x1b[1;33m{n}\x1b[0m\r\nA \x1b[32mgreen\x1b[0m and \x1b[38;5;208morange\x1b[0m place. Exits: {' '.join(ex)}\r\n")
        s.gmcp('Room.Info',{'num':int(s.room),'name':n,'zone':a,'exits':{d:int(t) for d,t in ex.items()},'terrain':'city'})
        s.gmcp('Char.Vitals',{'hp':s.hp,'maxhp':100,'mana':40,'maxmana':50,'moves':77,'maxmoves':90})
        s.prompt(f"\x1b[31m<{s.hp}hp>\x1b[0m ")
    def readline(s):
        buf=b''
        while True:
            d=s.c.recv(1)
            if not d: raise EOFError
            if d==b'\n': return buf.decode(errors='replace').strip()
            if d==bytes([IAC]):
                c=s.c.recv(2)
                if c[:1]==bytes([SB]):
                    while s.c.recv(1)!=bytes([SE]): pass
                elif c[:1]==bytes([DO]) and c[1]==86 and not s.z:
                    s.c.sendall(bytes([IAC,SB,86,IAC,SE])); s.z=zlib.compressobj()
                continue
            if d!=b'\r': buf+=d
    def run(s):
        try:
            s.c.sendall(bytes([IAC,WILL,201,IAC,WILL,86,IAC,WILL,EOR])); time.sleep(0.4)
            s.send("\x1b[1;36m=== Fake MUD (test server) ===\x1b[0m\r\n"); s.prompt("Name: ")
            name=s.readline(); s.send(bytes([IAC,WILL,1])); s.prompt("Password: "); s.readline()
            s.send(bytes([IAC,WONT,1])+f"\r\nWelcome, {name}!\r\n".encode()); s.show()
            while True:
                cmd=s.readline(); low=cmd.lower()
                dmap={'north':'n','south':'s','east':'e','west':'w','up':'u','down':'d'}; low=dmap.get(low,low)
                ex=ROOMS[s.room][2]
                if low in ex: s.room=ex[low]; s.show()
                elif low in ('n','s','e','w','u','d'): s.send("Alas, you cannot go that way.\r\n"); s.prompt("> ")
                elif low=='look': s.show()
                elif low.startswith('gossip '):
                    s.gmcp('Comm.Channel.Text',{'channel':'gossip','talker':'Zed','text':cmd[7:]}); s.prompt("> ")
                elif low=='flood':
                    for i in range(3000): s.send(f"\x1b[3{i%8}mLine {i} of the flood: lorem ipsum dolor sit amet, consectetur\x1b[0m\r\n")
                    s.send("FLOOD DONE\r\n"); s.prompt("> ")
                elif low=='quit': s.send("Bye.\r\n"); break
                elif low=='hit': s.hp=max(1,s.hp-17); s.send(f"The orc hits you for 17 damage\r\n"); s.show()
                else: s.send(f"You said: {cmd}\r\n"); s.prompt("> ")
        except Exception as e: print('client end',e)
        finally: s.c.close()
if __name__=='__main__':
    port=int(sys.argv[1]) if len(sys.argv)>1 and sys.argv[1].isdigit() else 4999
    srv=socket.socket(); srv.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); srv.bind(('127.0.0.1',port)); srv.listen(5)
    print('fake mud on',port,flush=True)
    while True: c,_=srv.accept(); Client(c).start()
